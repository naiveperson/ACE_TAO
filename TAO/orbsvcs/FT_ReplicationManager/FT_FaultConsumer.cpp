/* -*- C++ -*- */
//=============================================================================
/**
 *  @file    FT_FaultConsumer.cpp
 *
 *  $Id$
 *
 *  This file is part of TAO's implementation of Fault Tolerant CORBA.
 *  This file provides the implementation of the TAO::FT_FaultConsumer
 *  class.  The TAO::FT_FaultConsumer connects to the FaultNotifier to
 *  receive fault reports.  It interacts with the ReplicationManager
 *  to process fault reports (e.g., to set a new primary on an object
 *  group or to create a new member of an object group).
 *
 *  @author Steve Totten <totten_s@ociweb.com>
 */
//=============================================================================

#include "FT_FaultConsumer.h"
#include "orbsvcs/FT_ReplicationManagerC.h"
#include "orbsvcs/FT_ReplicationManager/FT_FaultAnalyzer.h"
#include "tao/debug.h"

ACE_RCSID (FT_FaultConsumer,
           FT_FaultConsumer,
           "$Id$")

/// Default constructor.
TAO::FT_FaultConsumer::FT_FaultConsumer ()
  : poa_ (PortableServer::POA::_nil ())
  , fault_notifier_ (FT::FaultNotifier::_nil ())
  , fault_analyzer_ (0)
  , consumer_id_ (0)
  , consumer_ref_ (CosNotifyComm::StructuredPushConsumer::_nil ())
  , notifications_ (0)
{
}

/// Destructor.
TAO::FT_FaultConsumer::~FT_FaultConsumer ()
{
}

/**
* Connect to the FT::FaultNotifier.
* Note: We make the following assumptions about what the
* application will do:
* - Create an instance of this consumer class.
* - Obtain the object reference of the FaultNotifier to which this
*   consumer should connect.
* - Call this init() method, passing it the POA with which we
*   have been activated, the FaultNotifier, and ReplicationManager
*   object references.
*/
int TAO::FT_FaultConsumer::init (
  PortableServer::POA_ptr poa,
  FT::FaultNotifier_ptr fault_notifier,
  TAO::FT_FaultAnalyzer * fault_analyzer
  ACE_ENV_ARG_DECL)
{

  if (TAO_debug_level > 1)
  {
    ACE_DEBUG ((LM_DEBUG,
      ACE_TEXT (
        "Enter TAO::FT_FaultConsumer::init.\n")
    ));
  }

  ACE_ASSERT (!CORBA::is_nil (poa));
  ACE_ASSERT (!CORBA::is_nil (fault_notifier));
  ACE_ASSERT (fault_analyzer != 0);

  // Duplicate the object references passed in.
  this->poa_ =
    PortableServer::POA::_duplicate (poa);
  this->fault_notifier_ =
    FT::FaultNotifier::_duplicate (fault_notifier);

  // We have no ownership responsibilities for the Fault Analyzer.
  this->fault_analyzer_ = fault_analyzer;

  //@@ Should this init() method activate the consumer in the POA, or
  // should the application do that?
  // I don't think this object can activate itself because it doesn't
  // know the policies on the POA.  So, we assume the application has
  // already activated us.
  //@@ For now, let's try just activating it in the POA.

  // Activate this consumer in the POA.
  this->object_id_ = this->poa_->activate_object (this ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);
  CORBA::Object_var obj =
    this->poa_->id_to_reference (this->object_id_.in() ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);

  // Narrow it to CosNotifyComm::StructuredPushConsumer.
  this->consumer_ref_ = CosNotifyComm::StructuredPushConsumer::_narrow (
    obj.in() ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);

  // Subscribe to the FaultNotifier.
  CosNotifyFilter::Filter_var filter = CosNotifyFilter::Filter::_nil ();
  this->consumer_id_ = fault_notifier_->connect_structured_fault_consumer (
    this->consumer_ref_.in(), filter.in () ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);

  if (TAO_debug_level > 1)
  {
    ACE_DEBUG ((LM_DEBUG,
      ACE_TEXT (
        "Leave TAO::FT_FaultConsumer::init.\n")
    ));
  }

  // Success.
  return 0;
}

/**
* Clean house for process shut down.
* - Disconnect from FT::FaultNotifier.
* - Deactivate from the POA.
*/
int TAO::FT_FaultConsumer::fini (ACE_ENV_SINGLE_ARG_DECL)
{

  if (TAO_debug_level > 1)
  {
    ACE_DEBUG ((LM_DEBUG,
      ACE_TEXT ("Enter TAO::FT_FaultConsumer::fini.\n")
    ));
  }

  // Disconnect from the FaultNotifier.
  // Swallow any exception.
  ACE_TRY_NEW_ENV
  {
    if (!CORBA::is_nil (this->fault_notifier_.in()))
    {

      if (TAO_debug_level > 1)
      {
        ACE_DEBUG ((LM_DEBUG,
          ACE_TEXT (
            "TAO::FT_FaultConsumer::fini: "
            "Disconnecting consumer from FaultNotifier.\n")
        ));
      }

      this->fault_notifier_->disconnect_consumer (
        this->consumer_id_ ACE_ENV_ARG_PARAMETER);
      ACE_TRY_CHECK;

      if (TAO_debug_level > 1)
      {
        ACE_DEBUG ((LM_DEBUG,
          ACE_TEXT (
            "TAO::FT_FaultConsumer::fini: "
            "Deactivating from POA.\n")
        ));
      }

      // Deactivate ourself from the POA.
      this->poa_->deactivate_object (
        this->object_id_.in() ACE_ENV_ARG_PARAMETER);
      ACE_TRY_CHECK;
    }
  }
  ACE_CATCHANY
  {
    ACE_PRINT_EXCEPTION (ACE_ANY_EXCEPTION,
      ACE_TEXT (
        "TAO::FT_FaultConsumer::fini: "
        "Error disconnecting from notifier (ignored).\n")
    );
  }
  ACE_ENDTRY;
  ACE_CHECK_RETURN(1);

  if (TAO_debug_level > 1)
  {
    ACE_DEBUG ((LM_DEBUG,
      ACE_TEXT (
        "TAO::FT_FaultConsumer::fini: "
        "Setting our object reference to nil.\n")
    ));
  }

  this->consumer_ref_ = CosNotifyComm::StructuredPushConsumer::_nil ();

  if (TAO_debug_level > 1)
  {
    ACE_DEBUG ((LM_DEBUG,
      ACE_TEXT ("Leave TAO::FT_FaultConsumer::fini.\n")
    ));
  }

  // Success.
  return 0;
}

CosNotifyComm::StructuredPushConsumer_ptr
TAO::FT_FaultConsumer::consumer_ref ()
{
  return CosNotifyComm::StructuredPushConsumer::_duplicate (
      this->consumer_ref_.in ());
}

size_t TAO::FT_FaultConsumer::notifications () const
{
  return this->notifications_;
}


///////////////////
// CORBA operations

// Receive and process an incoming fault event from the Fault Notifier.
// First, we validate the event to make sure it is something we can
// handle.  Then, we analyze it.  If it is not an event we can handle,
// we simply log the error and drop the event.
void TAO::FT_FaultConsumer::push_structured_event (
  const CosNotification::StructuredEvent &event
  ACE_ENV_ARG_DECL_NOT_USED
  )
  ACE_THROW_SPEC ((CORBA::SystemException, CosEventComm::Disconnected))
{
  // Debugging support.
  this->notifications_ += 1;
  if (TAO_debug_level > 1)
  {
    ACE_DEBUG ((LM_DEBUG,
      ACE_TEXT (
        "TAO::FT_FaultConsumer::push_structured_event: "
        "Received Fault notification(%d):\n"),
      ACE_static_cast (unsigned int, this->notifications_)
    ));
  }

  int result = 0;

  // Make sure it is an event type we can handle.
  if (result == 0)
  {
    result = this->fault_analyzer_->validate_event_type (event);
    if (result != 0)
    {
      ACE_ERROR ((LM_ERROR,
        ACE_TEXT (
          "TAO::FT_FaultConsumer::push_structured_event: "
          "Received invalid fault event type.\n")
      ));
    }
  }

  // Analyze the event.
  if (result == 0)
  {
    result = this->fault_analyzer_->analyze_fault_event (event);
    if (result != 0)
    {
      ACE_ERROR ((LM_ERROR,
        ACE_TEXT (
          "TAO::FT_FaultConsumer::push_structured_event: "
          "Could not analyze fault event.\n")
      ));
    }
  }

  return;
}

void TAO::FT_FaultConsumer::offer_change (
    const CosNotification::EventTypeSeq & added,
    const CosNotification::EventTypeSeq & removed
    ACE_ENV_ARG_DECL_NOT_USED
  )
  ACE_THROW_SPEC ((CORBA::SystemException, CosNotifyComm::InvalidEventType))
{
  ACE_UNUSED_ARG (added);
  ACE_UNUSED_ARG (removed);
  ACE_DEBUG ((LM_DEBUG,
    ACE_TEXT("TAO::FT_FaultConsumer::offer_change() call ignored.\n")
  ));
}

void TAO::FT_FaultConsumer::disconnect_structured_push_consumer (
    ACE_ENV_SINGLE_ARG_DECL_NOT_USED
  )
  ACE_THROW_SPEC ((CORBA::SystemException))
{
  //TODO: For now, we are just ignoring the disconnect callback.
  ACE_DEBUG ((LM_DEBUG,
    ACE_TEXT("TAO::FT_FaultConsumer::disconnect_structured_push_consumer() "
             "call ignored.\n")
    ));
}

