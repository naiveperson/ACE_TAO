// $Id$
#include "tao/TAO.h"
#include "AVStreams_i.h"
#include "sfp.h"
#include "MCast.h"
#include "Nil.h"
#include "RTP.h"

#if !defined (__ACE_INLINE__)
#include "Transport.i"
#endif /* __ACE_INLINE__ */

//------------------------------------------------------------
// TAO_AV_UDP_Flow_Handler
//------------------------------------------------------------

TAO_AV_UDP_Flow_Handler::TAO_AV_UDP_Flow_Handler (TAO_AV_Callback *callback)

  :TAO_AV_Flow_Handler(callback)
{
  ACE_NEW (this->transport_,
           TAO_AV_UDP_Transport (this));
//   ACE_NEW (this->sock_dgram_,
//            ACE_SOCK_Dgram);
}

TAO_AV_UDP_Flow_Handler::~TAO_AV_UDP_Flow_Handler (void)
{
  delete this->transport_;
  //  delete this->sock_dgram_;
}

TAO_AV_Transport *
TAO_AV_UDP_Flow_Handler::transport (void)
{
  return this->transport_;
}

int
TAO_AV_UDP_Flow_Handler::handle_input (ACE_HANDLE /*fd*/)
{
  size_t size = 2*this->transport_->mtu ();
  ACE_Message_Block *frame = 0;
  ACE_NEW_RETURN (frame,
                  ACE_Message_Block (size),
                  -1);
  int n = this->transport_->recv (frame->rd_ptr (),
                                  frame->size ());
  if (n == -1)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_UDP_Flow_Handler::handle_input recv failed\n"),-1);
  if (n == -1)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_UDP_Flow_Handler::handle_input connection closed\n"),-1);
  frame->wr_ptr (n);
  this->callback_->receive_frame (frame);
  return 0;
}

int
TAO_AV_UDP_Flow_Handler::handle_timeout (const ACE_Time_Value &tv, 
                                               const void *arg)
{
  return TAO_AV_Flow_Handler::handle_timeout (tv,arg);
}

int
TAO_AV_UDP_Flow_Handler::set_remote_address (ACE_Addr *address)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_UDP_Flow_Handler::set_remote_address\n"));
  ACE_INET_Addr *inet_addr = ACE_dynamic_cast (ACE_INET_Addr*,address);
  this->peer_addr_ = *inet_addr;
  TAO_AV_UDP_Transport *transport = ACE_dynamic_cast (TAO_AV_UDP_Transport*,this->transport_);
  return transport->set_remote_address (*inet_addr);
}


ACE_HANDLE
TAO_AV_UDP_Flow_Handler::get_handle (void) const
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_UDP_Flow_Handler::get_handle:%d\n",this->sock_dgram_.get_handle ()));
  return this->sock_dgram_.get_handle () ;
}

//------------------------------------------------------------
// TAO_AV_Core
//------------------------------------------------------------

TAO_AV_Core::TAO_AV_Core (void)
  :connector_registry_ (0),
   acceptor_registry_ (0)
{
}

TAO_AV_Core::~TAO_AV_Core (void)
{
  delete this->connector_registry_;
  delete this->acceptor_registry_;
}

int
TAO_AV_Core::init (int &argc,
                   char **argv,
                   CORBA::Environment &env)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_Core::init "));
  // Init the orb manager.
  int result = this->orb_manager_.init (argc,argv,env);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"orb_manager::init"),result);
  ACE_NEW_RETURN (this->connector_registry_,
                  TAO_AV_Connector_Registry,
                  -1);
  ACE_NEW_RETURN (this->acceptor_registry_,
                  TAO_AV_Acceptor_Registry,
                  -1);
  this->orb_ = this->orb_manager_.orb ();
  this->reactor (this->orb_->orb_core ()->reactor ());
  this->init_protocol_factories ();
  return 0;
}

int
TAO_AV_Core::init_forward_flows (TAO_Base_StreamEndPoint *endpoint,
                                 TAO_AV_FlowSpecSet &flow_spec_set,
                                 TAO_AV_Core::EndPoint direction,
                                 AVStreams::flowSpec &flow_spec)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_Core::init_forward_flows\n"));
  TAO_AV_FlowSpecSet address_flow_set;
  TAO_AV_FlowSpecSet flow_set;
  TAO_AV_FlowSpecSetItor end = flow_spec_set.end ();
  for (TAO_AV_FlowSpecSetItor start = flow_spec_set.begin ();
       start != end; ++start)
    {
      TAO_FlowSpec_Entry *entry = (*start);
      ACE_Addr *address = entry->address ();
      if (address != 0)
        {
          if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"address given for flow %s",entry->flowname ()));
          address_flow_set.insert (entry);
        }
      else
        flow_set.insert (entry);
    }
  int result = -1;
  switch (direction)
    {
    case TAO_AV_Core::TAO_AV_ENDPOINT_A:
      if (address_flow_set.size () > 0)
        {
          result = this->acceptor_registry_->open (endpoint,
                                                   this,
                                                   address_flow_set);
          TAO_AV_FlowSpecSetItor end = address_flow_set.end ();
          for (TAO_AV_FlowSpecSetItor start = address_flow_set.begin ();
               start != end; ++start)
            {
              TAO_FlowSpec_Entry *entry = (*start);
              switch (entry->direction ())
                {
                case TAO_FlowSpec_Entry::TAO_AV_DIR_IN:
                  {
                    if (entry->handler () != 0)
                      {
                        // For IN flows on the A side we should remove the handlers from the reactor.
                        ACE_Event_Handler *event_handler = entry->handler ()->event_handler ();
                        result = event_handler->reactor ()->remove_handler (event_handler,
                                                                            ACE_Event_Handler::READ_MASK);
                        if (result < 0)
                          if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_Core::init_forward_flows: remove_handler failed\n"));
                      }
                  }
                default:
                  break;
                }
            }
        }
      break;
    case TAO_AV_Core::TAO_AV_ENDPOINT_B:
      {
        if (address_flow_set.size () > 0)
          {
            result = this->connector_registry_->open (endpoint,
                                                      this,
                                                      address_flow_set);
            if (result == -1)
              ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_Core::init_Forward_flows: connector_registry open failed\n"),-1);
            TAO_AV_FlowSpecSetItor end = address_flow_set.end ();
            for (TAO_AV_FlowSpecSetItor start = address_flow_set.begin ();
                 start != end; ++start)
              {
                TAO_FlowSpec_Entry *entry = (*start);
                switch (entry->direction ())
                  {
                  case TAO_FlowSpec_Entry::TAO_AV_DIR_OUT:
                    {
                    if (entry->handler () != 0)
                      {
                        // @@Naga: This wont be called in the case of Full Profile.
                        // For IN flows on the A side we should remove the handlers from the reactor.
                        ACE_Event_Handler *event_handler = entry->handler ()->event_handler ();
                        result = event_handler->reactor ()->remove_handler (event_handler,
                                                                            ACE_Event_Handler::READ_MASK);
                        if (result < 0)
                          if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_Core::init_forward_flows: remove_handler failed\n"));
                      }
                    }
                  default:
                    break;
                  }
              }

          }
        if (flow_set.size () > 0)
          {
            result = this->acceptor_registry_->open (endpoint,
                                                     this,
                                                     flow_set);
            if (result == -1)
              ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_Core::init_Forward_flows: Acceptor_registry open failed\n"),-1);
            TAO_AV_FlowSpecSetItor end = address_flow_set.end ();
            for (TAO_AV_FlowSpecSetItor start = address_flow_set.begin ();
                 start != end; ++start)
              {
                TAO_FlowSpec_Entry *entry = (*start);
                switch (entry->direction ())
                  {
                  case TAO_FlowSpec_Entry::TAO_AV_DIR_OUT:
                    {
                    if (entry->handler () != 0)
                      {
                        // For IN flows on the A side we should remove the handlers from the reactor.
                        ACE_Event_Handler *event_handler = entry->handler ()->event_handler ();
                        result = event_handler->reactor ()->remove_handler (event_handler,
                                                                            ACE_Event_Handler::READ_MASK);
                        if (result < 0)
                          if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_Core::init_forward_flows: remove_handler failed\n"));
                      }
                    }
                  default:
                    break;
                  }
              }
          }

        AVStreams::flowSpec new_flowspec (flow_spec_set.size ());
        int i=0;
        TAO_AV_FlowSpecSetItor connect_end = address_flow_set.end ();
        TAO_AV_FlowSpecSetItor connect = address_flow_set.begin ();
        for (;connect != connect_end;  ++connect)
          {
            ACE_Addr *local_addr;
            local_addr = (*connect)->get_local_addr ();
            if (result == 0)
              {
                TAO_Reverse_FlowSpec_Entry entry ((*connect)->flowname (),
                                                  (*connect)->direction_str (),
                                                  (*connect)->format (),
                                                  (*connect)->flow_protocol_str (),
                                                  (*connect)->carrier_protocol_str (),
                                                  local_addr);

                int len = new_flowspec.length ();
                if (i == len)
                  new_flowspec.length (len+1);
                new_flowspec [i++] = entry.entry_to_string ();
              }
          }
        connect_end = flow_set.end ();
        for (connect = flow_set.begin ();
             connect != connect_end;  ++connect)
          {
            ACE_Addr *local_addr;
            local_addr = (*connect)->get_local_addr ();
            if (result == 0)
              {
                TAO_Reverse_FlowSpec_Entry entry ((*connect)->flowname (),
                                                  (*connect)->direction_str (),
                                                  (*connect)->format (),
                                                  (*connect)->flow_protocol_str (),
                                                  (*connect)->carrier_protocol_str (),
                                                  local_addr);

                int len = new_flowspec.length ();
                if (i == len)
                  new_flowspec.length (len+1);
                new_flowspec [i++] = entry.entry_to_string ();
              }
          }
        // Change the reverse flow spec to be sent.
        flow_spec = new_flowspec;
      }
      break;
    default:
      break;
    }
  return 0;
}

int
TAO_AV_Core::init_reverse_flows (TAO_Base_StreamEndPoint *endpoint,
                                 TAO_AV_FlowSpecSet &forward_flow_spec_set,
                                 TAO_AV_FlowSpecSet &reverse_flow_spec_set,
                                 TAO_AV_Core::EndPoint direction)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"(%P|%t)TAO_AV_Core::init_reverse_flows\n"));
  TAO_AV_FlowSpecSet acceptor_flow_set;
  TAO_AV_FlowSpecSet connector_flow_set;
  TAO_AV_FlowSpecSetItor end = reverse_flow_spec_set.end ();
  for (TAO_AV_FlowSpecSetItor start = reverse_flow_spec_set.begin ();
       start != end; ++start)
    {
      TAO_FlowSpec_Entry *entry = (*start);
      ACE_Addr *address = entry->address ();
      if (address != 0)
        {
          if (this->get_acceptor (entry->flowname ())!= 0)
            {
              ACE_Addr *address = entry->address ();
              TAO_FlowSpec_Entry *forward_entry =
                this->get_flow_spec_entry (forward_flow_spec_set,
                                           entry->flowname ());
              if (forward_entry != 0)
                forward_entry->set_peer_addr (address);
            }
          else
            connector_flow_set.insert (entry);
        }
    }
  int result = -1;
  switch (direction)
    {
    case TAO_AV_Core::TAO_AV_ENDPOINT_A:
      result = this->connector_registry_->open (endpoint,
                                                this,
                                                connector_flow_set);
      break;
    default:
      break;
    }
  if (result == -1)
    ACE_ERROR_RETURN ((LM_ERROR,"acceptor_registry::open"),-1);

  return 0;
}

TAO_FlowSpec_Entry *
TAO_AV_Core::get_flow_spec_entry (TAO_AV_FlowSpecSet &flow_spec_set,
                                  const char *flowname)
{
  TAO_AV_FlowSpecSetItor end = flow_spec_set.end ();
  TAO_AV_FlowSpecSetItor begin = flow_spec_set.begin ();
  for (;
       begin != end;
       ++begin)
    {
      if (ACE_OS::strcmp ((*begin)->flowname (),flowname) == 0)
        return (*begin);
    }
  return 0;
}

TAO_AV_Acceptor*
TAO_AV_Core::get_acceptor (const char *flowname)
{
  TAO_AV_AcceptorSetItor acceptor =
    this->acceptor_registry_->begin ();
  TAO_AV_AcceptorSetItor end =
    this->acceptor_registry_->end ();

  for (;acceptor != end; ++acceptor)
    {
      if (ACE_OS::strcmp ((*acceptor)->flowname (),flowname) == 0)
        return *acceptor;
    }
  return 0;
}

TAO_AV_Connector*
TAO_AV_Core::get_connector (const char *flowname)
{
  TAO_AV_ConnectorSetItor connector =
    this->connector_registry_->begin ();
  TAO_AV_ConnectorSetItor end =
    this->connector_registry_->end ();

  for (;connector != end; ++connector)
    {
      if (ACE_OS::strcmp ((*connector)->flowname (),flowname) == 0)
        return *connector;
    }
  return 0;
}

int
TAO_AV_Core::init_protocol_factories (void)
{
  TAO_AV_ProtocolFactorySetItor end = protocol_factories_.end ();
  TAO_AV_ProtocolFactorySetItor factory = protocol_factories_.begin ();

  if (factory == end)
    {
      TAO_AV_Protocol_Factory *udp_protocol_factory = 0;
      TAO_AV_Protocol_Item *udp_item = 0;

      ACE_NEW_RETURN (udp_protocol_factory,
                      TAO_AV_UDP_Protocol_Factory,
                      -1);
      ACE_NEW_RETURN (udp_item, TAO_AV_Protocol_Item ("UDP_Factory"), -1);
      udp_item->factory (udp_protocol_factory);

      this->protocol_factories_.insert (udp_item);

      TAO_AV_Protocol_Factory *tcp_protocol_factory = 0;
      TAO_AV_Protocol_Item *tcp_item = 0;

      ACE_NEW_RETURN (tcp_protocol_factory,
                      TAO_AV_TCP_Protocol_Factory,
                      -1);
      ACE_NEW_RETURN (tcp_item, TAO_AV_Protocol_Item ("TCP_Factory"), -1);
      tcp_item->factory (tcp_protocol_factory);

      this->protocol_factories_.insert (tcp_item);

      TAO_AV_Protocol_Factory *sfp_protocol_factory = 0;
      TAO_AV_Protocol_Item *sfp_item = 0;

      ACE_NEW_RETURN (sfp_protocol_factory,
                      TAO_SFP_UDP_Protocol_Factory,
                      -1);
      ACE_NEW_RETURN (sfp_item, TAO_AV_Protocol_Item ("SFP_UDP_Factory"), -1);
      sfp_item->factory (sfp_protocol_factory);

      this->protocol_factories_.insert (sfp_item);

      TAO_AV_Protocol_Factory *udp_mcast_protocol_factory = 0;
      TAO_AV_Protocol_Item *udp_mcast_item = 0;

      ACE_NEW_RETURN (udp_mcast_protocol_factory,
                      TAO_AV_UDP_MCast_Protocol_Factory,
                      -1);
      ACE_NEW_RETURN (udp_mcast_item, TAO_AV_Protocol_Item ("UDP_MCast_Factory"), -1);
      udp_mcast_item->factory (udp_mcast_protocol_factory);

      this->protocol_factories_.insert (udp_mcast_item);

      TAO_AV_Protocol_Factory *rtp_udp_protocol_factory = 0;
      TAO_AV_Protocol_Item *rtp_udp_item = 0;

      ACE_NEW_RETURN (rtp_udp_protocol_factory,
                      TAO_AV_RTP_UDP_Protocol_Factory,
                      -1);
      ACE_NEW_RETURN (rtp_udp_item, TAO_AV_Protocol_Item ("RTP_UDP_Factory"), -1);
      rtp_udp_item->factory (rtp_udp_protocol_factory);

      this->protocol_factories_.insert (rtp_udp_item);

      TAO_AV_Protocol_Factory *rtp_udp_mcast_protocol_factory = 0;
      TAO_AV_Protocol_Item *rtp_udp_mcast_item = 0;

      ACE_NEW_RETURN (rtp_udp_mcast_protocol_factory,
                      TAO_AV_RTP_UDP_MCast_Protocol_Factory,
                      -1);
      ACE_NEW_RETURN (rtp_udp_mcast_item, TAO_AV_Protocol_Item ("RTP_UDP_MCast_Factory"), -1);
      rtp_udp_mcast_item->factory (rtp_udp_mcast_protocol_factory);

      this->protocol_factories_.insert (rtp_udp_mcast_item);

      TAO_AV_Protocol_Factory *sfp_udp_mcast_protocol_factory = 0;
      TAO_AV_Protocol_Item *sfp_udp_mcast_item = 0;

      ACE_NEW_RETURN (sfp_udp_mcast_protocol_factory,
                      TAO_SFP_UDP_MCast_Protocol_Factory,
                      -1);
      ACE_NEW_RETURN (sfp_udp_mcast_item, TAO_AV_Protocol_Item ("SFP_UDP_MCast_Factory"), -1);
      sfp_udp_mcast_item->factory (sfp_udp_mcast_protocol_factory);

      this->protocol_factories_.insert (sfp_udp_mcast_item);

      if (TAO_debug_level > 0)
        {
          if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,
                      "TAO (%P|%t) Loaded default protocol UDP_Factory\n"));
        }
    }
  return 0;
}


//------------------------------------------------------------
// TAO_AV_Protocol_Item
//------------------------------------------------------------
TAO_AV_Protocol_Item::TAO_AV_Protocol_Item (const ACE_CString &name)
  :   name_ (name),
      factory_ (0)
{
}

//------------------------------------------------------------
// TAO_AV_Connector_Registry
//------------------------------------------------------------

TAO_AV_Connector_Registry::TAO_AV_Connector_Registry (void)
{
}

int
TAO_AV_Connector_Registry::open (TAO_Base_StreamEndPoint *endpoint,
                                 TAO_AV_Core *av_core,
                                 TAO_AV_FlowSpecSet &flow_spec_set)
{
  TAO_AV_FlowSpecSetItor last_flowspec =
    flow_spec_set.end ();
  for (TAO_AV_FlowSpecSetItor flow_spec = flow_spec_set.begin ();
       flow_spec != last_flowspec;
       ++flow_spec)
    {
      TAO_FlowSpec_Entry *entry = (*flow_spec);
      ACE_Addr *address = entry->address ();
      TAO_AV_Core::Protocol protocol = entry->carrier_protocol ();
      if (address == 0)
        {
          // Protocol was specified without an endpoint.  According to
          // the "iioploc" spec, this is valid.  As such, we extend
          // this feature to all pluggable protocols.  All TAO
          // pluggable protocols are expected to have the ability to
          // create a default endpoint.

          ACE_ERROR_RETURN ((LM_ERROR,"Protocol was specified without an endpoint\n"),-1);
        }
      else
        {
          // Now get the list of avaliable protocol factories.
          TAO_AV_ProtocolFactorySetItor end =
            av_core->protocol_factories ()->end ();

          for (TAO_AV_ProtocolFactorySetItor factory =
                 av_core->protocol_factories ()->begin ();
               factory != end;
               ++factory)
            {
              if ((*factory)->factory ()->match_protocol (protocol))
                {
                  TAO_AV_Connector *connector =
                    (*factory)->factory ()->make_connector ();
                  if (connector != 0)
                    {
                      // add connector to list.
                      this->connectors_.insert (connector);

                      if (connector->open (endpoint,
                                           av_core) == -1)
                        return -1;
                      TAO_AV_Transport *transport = 0;
                      if (connector->connect (entry,
                                              transport) == -1)
                        return -1;
                      entry->transport (transport);
                      break;
                    }
                  else
                    ACE_ERROR_RETURN ((LM_ERROR,
                                       "(%P|%t) Unable to create an "
                                       "connector for <%s>\n",
                                       entry->flowname ()),
                                      -1);
                }
              else
                continue;
            }
        }
    }
  return 0;
}

int
TAO_AV_Connector_Registry::close_all (void)
{
  TAO_AV_ConnectorSetItor end =
    this->connectors_.end ();

  for (TAO_AV_ConnectorSetItor i = this->connectors_.begin ();
       i != end;
       ++i)
    {
      if (*i == 0)
        continue;

      (*i)->close ();

      delete *i;
    }

  this->connectors_.reset ();
  return 0;
}

/*
int
TAO_AV_Connector_Registry::connect (TAO_FlowSpec_Entry *flowspec,
                                    TAO_AV_Transport *&transport)
{
  // Find the appropriate connector object
  TAO_AV_Connector *connector =
    this->get_connector (flowspec->carrier_protocol ());

  return connector->connect (flowspec, transport);
}


TAO_AV_Connector *
TAO_AV_Connector_Registry::get_connector (TAO_AV_Core::Protocol protocol)
{
  TAO_AV_ConnectorSetItor end =
    this->connectors_.end ();
  TAO_AV_ConnectorSetItor connector =
    this->connectors_.begin ();

  for (;
       connector != end ;
       connector++)
    {
      if ((*connector)->carrier_protocol () == protocol)
        return *connector;
    }
  return 0;
}
*/

TAO_AV_Connector_Registry::~TAO_AV_Connector_Registry (void)
{
  this->close_all ();
}

//------------------------------------------------------------
// TAO_AV_Acceptor_Registry
//------------------------------------------------------------

TAO_AV_Acceptor_Registry::TAO_AV_Acceptor_Registry (void)
{
}

TAO_AV_Acceptor_Registry::~TAO_AV_Acceptor_Registry (void)
{
}

int
TAO_AV_Acceptor_Registry::open (TAO_Base_StreamEndPoint *endpoint,
                                TAO_AV_Core *av_core,
                                TAO_AV_FlowSpecSet &flow_spec_set)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_Acceptor_Registry::open"));
  TAO_AV_FlowSpecSetItor last_flowspec =
    flow_spec_set.end ();
  for (TAO_AV_FlowSpecSetItor flow_spec = flow_spec_set.begin ();
       flow_spec != last_flowspec;
       ++flow_spec)
    {
      TAO_FlowSpec_Entry *entry = (*flow_spec);
      ACE_Addr *address = entry->address ();
      TAO_AV_Core::Protocol protocol = entry->carrier_protocol ();
      if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_Acceptor_Registry::protocol for flow %s is %d",
                  entry->flowname (),protocol));
      if (address == 0)
        {
          this->open_default (endpoint,av_core, entry);
          continue;
        }
      else
        {
          // Now get the list of avaliable protocol factories.
          TAO_AV_ProtocolFactorySetItor end =
            av_core->protocol_factories ()->end ();

          for (TAO_AV_ProtocolFactorySetItor factory =
                 av_core->protocol_factories ()->begin ();
               factory != end;
               ++factory)
            {
              if ((*factory)->factory ()->match_protocol (protocol))
                {
                  TAO_AV_Acceptor *acceptor =
                    (*factory)->factory ()->make_acceptor ();
                  if (acceptor != 0)
                    {
                      // add acceptor to list.
                      this->acceptors_.insert (acceptor);

                      if (acceptor->open (endpoint,
                                          av_core,
                                          entry) == -1)
                        return -1;
                      break;
                    }
                  else
                    ACE_ERROR_RETURN ((LM_ERROR,
                                       "(%P|%t) Unable to create an "
                                       "acceptor for <%s>\n",
                                       entry->flowname ()),
                                      -1);
                }
              else
                continue;
            }
        }
    }
  return 0;
}

int
TAO_AV_Acceptor_Registry::open_default (TAO_Base_StreamEndPoint *endpoint,
                                        TAO_AV_Core *av_core,
                                        TAO_FlowSpec_Entry *entry)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_Acceptor_Registry::open_default "));
  // No endpoints were specified, we let each protocol pick its own
  // default...

  TAO_AV_ProtocolFactorySetItor end =
    av_core->protocol_factories ()->end ();

  // loop through loaded protocols looking for protocol_prefix
  for (TAO_AV_ProtocolFactorySetItor i =
         av_core->protocol_factories ()->begin ();
       i != end;
       ++i)
    {
      TAO_AV_Core::Protocol protocol = entry->carrier_protocol ();
      if (protocol != TAO_AV_Core::TAO_AV_NOPROTOCOL)
        {
          if (!(*i)->factory ()->match_protocol (protocol))
            {
              // If we have no matching protocol then keep searching
              // for one until the entire list of protocols has been
              // searched.

              if (TAO_debug_level > 0)
                ACE_ERROR ((LM_ERROR,
                            "TAO (%P|%t) Unable to match protocol prefix "
                            "for <%s>\n",
                            protocol));
              continue;
            }
        }

      // got it, make an acceptor
      TAO_AV_Acceptor *acceptor =
        (*i)->factory ()->make_acceptor ();

      if (acceptor == 0)
        {
          if (TAO_debug_level > 0)
            ACE_ERROR ((LM_ERROR,
                        "TAO (%P|%t) unable to create "
                        "an acceptor for <%d>\n",
                        protocol));
          continue;
        }

      if (acceptor->open_default (endpoint,
                                  av_core,
                                  entry) == -1)
        {
          if (TAO_debug_level > 0)
            ACE_ERROR ((LM_ERROR,
                        "TAO (%P|%t) unable to open "
                        "default acceptor for <%s>%p\n",
                        (*i)->protocol_name ().c_str (), ""));
          continue;
        }

      this->acceptors_.insert (acceptor);
    }

  if (this->acceptors_.size () == 0)
    {
      if (TAO_debug_level > 0)
        ACE_ERROR ((LM_ERROR,
                    "TAO (%P%t) cannot create any default acceptor\n"));
      return -1;
    }

  return 0;
}

int
TAO_AV_Acceptor_Registry::close_all (void)
{
  TAO_AV_AcceptorSetItor end =
                this->acceptors_.end ();

  for (TAO_AV_AcceptorSetItor i = this->acceptors_.begin ();
       i != end;
       ++i)
    {
      if (*i == 0)
        continue;

      (*i)->close ();

      delete *i;
    }

  this->acceptors_.reset ();
  return 0;
}

//----------------------------------------------------------------------
// TAO_AV_Transport
//----------------------------------------------------------------------

TAO_AV_Transport::TAO_AV_Transport (void)
{
}

// Virtual destructor.
TAO_AV_Transport::~TAO_AV_Transport (void)
{
}

ACE_Addr*
TAO_AV_Transport::get_local_addr (void)
{
  return 0;
}

//----------------------------------------------------------------------
// TAO_AV_Flow_Handler
//----------------------------------------------------------------------

TAO_AV_Flow_Handler::TAO_AV_Flow_Handler (TAO_AV_Callback *callback)
  :transport_ (0),
   callback_ (callback),
   protocol_object_ (0)
{
}

int
TAO_AV_Flow_Handler::set_remote_address (ACE_Addr */* address */)
{
  return 0;
}

int
TAO_AV_Flow_Handler::start (TAO_FlowSpec_Entry::Role role)
{
  this->callback_->handle_start ();
  switch (role)
    {
      // only for producer we register for the timeout.
    case TAO_FlowSpec_Entry::TAO_AV_PRODUCER:
      {
        ACE_Event_Handler *event_handler = this->event_handler ();
        ACE_Time_Value *tv = 0;
        this->callback_->get_timeout (tv,
                                      this->timeout_arg_);
        if (tv == 0)
          return 0;
        this->timer_id_ =  event_handler->reactor ()->schedule_timer (event_handler,
                                                                      0,
                                                                      *tv);
      }
      break;
    }
  return 0;
}

int
TAO_AV_Flow_Handler::stop (TAO_FlowSpec_Entry::Role role)
{
  this->callback_->handle_stop ();
  switch (role)
    {
    case TAO_FlowSpec_Entry::TAO_AV_PRODUCER:
      {
        int result =  this->event_handler ()->reactor ()->cancel_timer (this->timer_id_);
        if (result <  0)
          if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_Flow_Handler::stop:cancel_timer failed\n"));
      }
      break;
    }
  return 0;
}

int
TAO_AV_Flow_Handler::handle_timeout (const ACE_Time_Value &tv,
                                     const void *arg)
{
  this->callback_->handle_timeout (this->timeout_arg_);
  ACE_Event_Handler *event_handler = this->event_handler ();
  ACE_Time_Value *timeout = 0;
  this->callback_->get_timeout (timeout,
                                this->timeout_arg_);
  if (timeout == 0)
    return 0;
  this->timer_id_ =  event_handler->reactor ()->schedule_timer (event_handler,
                                                                0,
                                                                *timeout);
  return 0;
}

TAO_AV_Transport*
TAO_AV_Flow_Handler::transport (void)
{
  return this->transport_;
}

void
TAO_AV_Flow_Handler::protocol_object (TAO_AV_Protocol_Object *protocol_object)
{
  this->protocol_object_ = protocol_object;
}

TAO_AV_Protocol_Object*
TAO_AV_Flow_Handler::protocol_object (void)
{
  return this->protocol_object_;
}

// TAO_AV_Connector
TAO_AV_Connector::TAO_AV_Connector (void)
{
}

TAO_AV_Connector::~TAO_AV_Connector (void)
{
}

// TAO_AV_Acceptor
TAO_AV_Acceptor::TAO_AV_Acceptor (void)
{
}

TAO_AV_Acceptor::~TAO_AV_Acceptor (void)
{
}

// TAO_AV_Protocol_Factory
TAO_AV_Protocol_Factory::TAO_AV_Protocol_Factory (void)
{
}

TAO_AV_Protocol_Factory::~TAO_AV_Protocol_Factory (void)
{
}

//------------------------------------------------------------
// TAO_AV_UDP_Transport
//------------------------------------------------------------

TAO_AV_UDP_Transport::TAO_AV_UDP_Transport (void)
  :handler_ (0)
{
}

TAO_AV_UDP_Transport::TAO_AV_UDP_Transport (TAO_AV_UDP_Flow_Handler *handler)
  :handler_ (handler),
   addr_ (0)
{
}

TAO_AV_UDP_Transport::~TAO_AV_UDP_Transport (void)
{
}

int
TAO_AV_UDP_Transport::set_remote_address (const ACE_INET_Addr &address)
{
  this->peer_addr_ = address;
  return 0;
}

int
TAO_AV_UDP_Transport::open (ACE_Addr */*address*/)
{
  return 0;
}

int
TAO_AV_UDP_Transport::close (void)
{
  return 0;
}

int
TAO_AV_UDP_Transport::mtu (void)
{
  return ACE_MAX_DGRAM_SIZE;
}

ACE_Addr*
TAO_AV_UDP_Transport::get_peer_addr (void)
{
  return &this->peer_addr_;
}

ssize_t
TAO_AV_UDP_Transport::send (const ACE_Message_Block *mblk, ACE_Time_Value *)
{
  // For the most part this was copied from GIOP::send_request and
  // friends.

  iovec iov[IOV_MAX];
  int iovcnt = 0;
  ssize_t n = 0;
  ssize_t nbytes = 0;

  for (const ACE_Message_Block *i = mblk;
       i != 0;
       i = i->cont ())
    {
      // Make sure there is something to send!
      if (i->length () > 0)
        {
          iov[iovcnt].iov_base = i->rd_ptr ();
          iov[iovcnt].iov_len  = i->length ();
          iovcnt++;

          // The buffer is full make a OS call.  @@ TODO this should
          // be optimized on a per-platform basis, for instance, some
          // platforms do not implement writev() there we should copy
          // the data into a buffer and call send_n(). In other cases
          // there may be some limits on the size of the iovec, there
          // we should set IOV_MAX to that limit.
          if (iovcnt == IOV_MAX)
            {
              n = this->handler_->get_socket ()->send ((const iovec *) iov,
                                                        iovcnt,
                                                        this->peer_addr_);

              if (n < 1)
                return n;

              nbytes += n;
              iovcnt = 0;
            }
        }
    }

  // Check for remaining buffers to be sent!
  if (iovcnt != 0)
    {
      n = this->handler_->get_socket ()->send ((const iovec *) iov,
                                               iovcnt,
                                               this->peer_addr_);

      if (n < 1)
        return n;

      nbytes += n;
    }

  return nbytes;
}

ssize_t
TAO_AV_UDP_Transport::send (const char *buf,
                            size_t len,
                            ACE_Time_Value *)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_UDP_Transport::send "));
  char addr [BUFSIZ];
  this->peer_addr_.addr_to_string (addr,BUFSIZ);
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"to %s\n",addr));

  return this->handler_->get_socket ()->send (buf, len,this->peer_addr_);
}

ssize_t
TAO_AV_UDP_Transport::send (const iovec *iov,
                          int iovcnt,
                          ACE_Time_Value *)
{
  return this->handler_->get_socket ()->send ((const iovec *) iov,
                                              iovcnt,
                                              this->peer_addr_);

}

ssize_t
TAO_AV_UDP_Transport::recv (char *buf,
                            size_t len,
                            ACE_Time_Value *)
{
  return this->handler_->get_socket ()->recv (buf, len,this->peer_addr_);
}

ssize_t
TAO_AV_UDP_Transport::recv (char *buf,
                            size_t len,
                            int flags,
                            ACE_Time_Value *timeout)
{
  return this->handler_->get_socket ()->recv (buf,
                                              len,
                                              this->peer_addr_,
                                              flags,
                                              timeout);
}

ssize_t
TAO_AV_UDP_Transport::recv (iovec *iov,
                            int /*iovcnt*/,
                            ACE_Time_Value *timeout)
{
  return handler_->get_socket ()->recv (iov,this->peer_addr_,0,timeout);
}

//------------------------------------------------------------
// TAO_AV_TCP_Transport
//------------------------------------------------------------

TAO_AV_TCP_Transport::TAO_AV_TCP_Transport (void)
  :handler_ (0)
{
}

TAO_AV_TCP_Transport::TAO_AV_TCP_Transport (TAO_AV_TCP_Flow_Handler *handler)
  :handler_ (handler)
{
}

TAO_AV_TCP_Transport::~TAO_AV_TCP_Transport (void)
{
}

int
TAO_AV_TCP_Transport::open (ACE_Addr */*address*/)
{
  return 0;
}

int
TAO_AV_TCP_Transport::close (void)
{
  return 0;
}

int
TAO_AV_TCP_Transport::mtu (void)
{
  return -1;
}

ACE_Addr*
TAO_AV_TCP_Transport::get_peer_addr (void)
{
  return 0;
}

ssize_t
TAO_AV_TCP_Transport::send (const ACE_Message_Block *mblk, ACE_Time_Value *)
{
  // For the most part this was copied from GIOP::send_request and
  // friends.

  iovec iov[IOV_MAX];
  int iovcnt = 0;
  ssize_t n = 0;
  ssize_t nbytes = 0;

  for (const ACE_Message_Block *i = mblk;
       i != 0;
       i = i->cont ())
    {
      // Make sure there is something to send!
      if (i->length () > 0)
        {
          iov[iovcnt].iov_base = i->rd_ptr ();
          iov[iovcnt].iov_len  = i->length ();
          iovcnt++;

          // The buffer is full make a OS call.  @@ TODO this should
          // be optimized on a per-platform basis, for instance, some
          // platforms do not implement writev() there we should copy
          // the data into a buffer and call send_n(). In other cases
          // there may be some limits on the size of the iovec, there
          // we should set IOV_MAX to that limit.
          if (iovcnt == IOV_MAX)
            {
              n = this->handler_->peer ().sendv_n ((const iovec *) iov,
                                                   iovcnt);
              if (n < 1)
                return n;

              nbytes += n;
              iovcnt = 0;
            }
        }
    }

  // Check for remaining buffers to be sent!
  if (iovcnt != 0)
    {
      n = this->handler_->peer ().sendv_n ((const iovec *) iov,
                                           iovcnt);
      if (n < 1)
        return n;

      nbytes += n;
    }

  return nbytes;
}

ssize_t
TAO_AV_TCP_Transport::send (const char *buf,
                            size_t len,
                            ACE_Time_Value *)
{
  return this->handler_->peer ().send_n (buf, len);
}

ssize_t
TAO_AV_TCP_Transport::send (const iovec *iov,
                          int iovcnt,
                          ACE_Time_Value *)
{
  return this->handler_->peer ().sendv_n ((const iovec *) iov,
                                          iovcnt);
}

ssize_t
TAO_AV_TCP_Transport::recv (char *buf,
                          size_t len,
                          ACE_Time_Value *)
{
  return this->handler_->peer ().recv (buf, len);
}

ssize_t
TAO_AV_TCP_Transport::recv (char *buf,
                            size_t len,
                            int flags,
                            ACE_Time_Value *)
{
  return this->handler_->peer ().recv (buf,
                                       len,
                                       flags);
}

ssize_t
TAO_AV_TCP_Transport::recv (iovec *iov,
                            int iovcnt,
                            ACE_Time_Value *)
{
  return handler_->peer ().recvv_n (iov, iovcnt);
}

//------------------------------------------------------------
// TAO_AV_UDP_Acceptor
//------------------------------------------------------------

TAO_AV_UDP_Acceptor::TAO_AV_UDP_Acceptor (void)
{
}

TAO_AV_UDP_Acceptor::~TAO_AV_UDP_Acceptor (void)
{
}

int
TAO_AV_UDP_Acceptor::make_svc_handler (TAO_AV_UDP_Flow_Handler *&udp_handler)
{
  TAO_AV_Callback *callback = 0;
  if (this->endpoint_ != 0)
    {
      this->endpoint_->get_callback (this->flowname_.c_str (),
                                     callback);
      ACE_NEW_RETURN (udp_handler,
                      TAO_AV_UDP_Flow_Handler (callback),
                      -1);
      callback->transport (udp_handler->transport ());
      TAO_AV_Protocol_Object *object =0;
      ACE_NEW_RETURN (object,
                      TAO_AV_UDP_Object (callback,
                                         udp_handler->transport ()),
                      -1);
      // @@: Call open on callback with protocol object and transport.
      udp_handler->protocol_object (object);
      callback->protocol_object (object);
      this->endpoint_->set_protocol_object (this->flowname_.c_str (),
                                            object);
      this->endpoint_->set_handler (this->flowname_.c_str (),udp_handler);
      this->entry_->protocol_object (object);
    }
  return 0;
}

int
TAO_AV_UDP_Acceptor::open (TAO_Base_StreamEndPoint *endpoint,
                           TAO_AV_Core *av_core,
                           TAO_FlowSpec_Entry *entry)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_UDP_Acceptor::open "));
  this->av_core_ = av_core;
  this->endpoint_ = endpoint;
  this->entry_ = entry;
  this->flowname_ = entry->flowname ();
  ACE_Addr *address = entry->address ();
  ACE_INET_Addr *inet_addr = (ACE_INET_Addr *) address;
  inet_addr->set (inet_addr->get_port_number (),
                  inet_addr->get_host_name ());
  char buf[BUFSIZ];
  inet_addr->addr_to_string (buf,
                             BUFSIZ);
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_UDP_Acceptor::open: %s",
              buf));
  int result = this->acceptor_.open (this,
                                     av_core->reactor (),
                                     *address,
                                     this->handler_);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_UDP_Acceptor::open failed"),-1);
  entry->handler (this->handler_);
  entry->set_local_addr (address);
  return 0;
}

int
TAO_AV_UDP_Acceptor::open_default (TAO_Base_StreamEndPoint *endpoint,
                                   TAO_AV_Core *av_core,
                                   TAO_FlowSpec_Entry *entry)
{
  this->av_core_ = av_core;
  this->endpoint_ = endpoint;
  this->entry_ = entry;
  this->flowname_ = entry->flowname ();
  ACE_INET_Addr *address;
  ACE_NEW_RETURN (address,
                  ACE_INET_Addr ("0"),
                  -1);
  int result = this->acceptor_.open (this,
                                     av_core->reactor (),
                                     *address,
                                     this->handler_);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_UDP_Acceptor::open failed"),-1);
  address->set (address->get_port_number (),
               address->get_host_name ());
  char buf[BUFSIZ];
  address->addr_to_string (buf,BUFSIZ);
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_UDP_Acceptor::open_default: %s\n",buf));
  entry->handler (this->handler_);
  entry->set_local_addr (address);
  return 0;
}

int
TAO_AV_UDP_Acceptor::close (void)
{
  return 0;
}

//------------------------------------------------------------
// TAO_AV_Dgram_Acceptor
//------------------------------------------------------------

int
TAO_AV_Dgram_Acceptor::open (TAO_AV_UDP_Acceptor *acceptor,
                             ACE_Reactor *reactor,
                             ACE_Addr &address,
                             TAO_AV_UDP_Flow_Handler *&handler)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_Dgram_Acceptor::open "));
  this->acceptor_ = acceptor;
  this->reactor_ = reactor;
  int result = 0;
  result = this->make_svc_handler (handler);
  result = handler->open (address);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"SOCK_Dgram::open failed\n"),-1);
  int sndbufsize = ACE_DEFAULT_MAX_SOCKET_BUFSIZ;
  int rcvbufsize = ACE_DEFAULT_MAX_SOCKET_BUFSIZ;

  if (handler->get_socket ()->set_option (SOL_SOCKET,
                                          SO_SNDBUF,
                                          (void *) &sndbufsize,
                                          sizeof (sndbufsize)) == -1
      && errno != ENOTSUP)
    return 0;

  else if (handler->get_socket ()->set_option (SOL_SOCKET,
                                               SO_RCVBUF,
                                               (void *) &rcvbufsize,
                                               sizeof (rcvbufsize)) == -1
           && errno != ENOTSUP)
    return 0;
  result = this->activate_svc_handler (handler);
  if (result < 0)
    return result;
  result = handler->get_socket ()->get_local_addr (address);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"SOCK_Dgram::get_local_addr failed\n"),-1);
  return 0;
}


int
TAO_AV_Dgram_Acceptor::activate_svc_handler (TAO_AV_UDP_Flow_Handler *handler)
{
  ACE_Event_Handler *event_handler = handler;
  //  ACE_HANDLE fd = event_handler->get_handle ();
  int result = this->reactor_->register_handler (event_handler,
                                                 ACE_Event_Handler::READ_MASK);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_Dgram_Acceptor::activate_svc_handler failed\n"),result);
  event_handler->reactor (this->reactor_);
  return 0;
}


int
TAO_AV_Dgram_Acceptor::make_svc_handler (TAO_AV_UDP_Flow_Handler *&handler)
{
  return this->acceptor_->make_svc_handler (handler);
}

//------------------------------------------------------------
// TAO_AV_Dgram_Connector
//------------------------------------------------------------

int
TAO_AV_Dgram_Connector::open (TAO_AV_UDP_Connector *connector,
                              ACE_Reactor *reactor)
{
  this->connector_ = connector;
  this->reactor_ = reactor;
  return 0;
}

int
TAO_AV_Dgram_Connector::connect (TAO_AV_UDP_Flow_Handler *&handler,
                                 ACE_Addr &remote_addr,
                                 ACE_Addr &local_addr)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_Dgram_Connector::connect "));
  int result = 0;
  this->make_svc_handler (handler);
  result = handler->open (local_addr);
  // set the socket buffer sizes to 64k.
  int sndbufsize = ACE_DEFAULT_MAX_SOCKET_BUFSIZ;
  int rcvbufsize = ACE_DEFAULT_MAX_SOCKET_BUFSIZ;

  if (handler->get_socket ()->set_option (SOL_SOCKET,
                                          SO_SNDBUF,
                                          (void *) &sndbufsize,
                                          sizeof (sndbufsize)) == -1
      && errno != ENOTSUP)
    return 0;

  else if (handler->get_socket ()->set_option (SOL_SOCKET,
                                               SO_RCVBUF,
                                               (void *) &rcvbufsize,
                                               sizeof (rcvbufsize)) == -1
           && errno != ENOTSUP)
    return 0;

  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"SOCK_Dgram::open failed\n"),-1);
  result = this->activate_svc_handler (handler);
  if (result < 0)
    return result;
  handler->set_remote_address  (&remote_addr);
  result = handler->get_socket ()->get_local_addr (local_addr);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_Dgram_Connector::open: get_local_addr failed\n"),result);
  return 0;
}

int
TAO_AV_Dgram_Connector::activate_svc_handler (TAO_AV_UDP_Flow_Handler *handler)
{
  int result = this->reactor_->register_handler (handler,
                                                 ACE_Event_Handler::READ_MASK);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_Dgram_Connector::activate_svc_handler failed\n"),result);
  handler->reactor (this->reactor_);
  return 0;
}

int
TAO_AV_Dgram_Connector::make_svc_handler (TAO_AV_UDP_Flow_Handler *&handler)
{
  return this->connector_->make_svc_handler (handler);
}

//------------------------------------------------------------
// TAO_AV_UDP_Connector
//------------------------------------------------------------
TAO_AV_UDP_Connector::TAO_AV_UDP_Connector (void)
{
}

TAO_AV_UDP_Connector::~TAO_AV_UDP_Connector (void)
{
}

int
TAO_AV_UDP_Connector::make_svc_handler (TAO_AV_UDP_Flow_Handler *&udp_handler)
{
  TAO_AV_Callback *callback = 0;
  if (this->endpoint_ != 0)
    {
      this->endpoint_->get_callback (this->flowname_.c_str (),
                                     callback);
      ACE_NEW_RETURN (udp_handler,
                      TAO_AV_UDP_Flow_Handler (callback),
                      -1);
      callback->transport (udp_handler->transport ());
      TAO_AV_Protocol_Object *object =0;
      ACE_NEW_RETURN (object,
                      TAO_AV_UDP_Object (callback,
                                         udp_handler->transport ()),
                      -1);
      udp_handler->protocol_object (object);
      callback->protocol_object (object);
      this->endpoint_->set_protocol_object (this->flowname_.c_str (),
                                            object);
      this->endpoint_->set_handler (this->flowname_.c_str (),udp_handler);
      this->entry_->protocol_object (object);
    }
  return 0;
}

int
TAO_AV_UDP_Connector::open (TAO_Base_StreamEndPoint *endpoint,
                            TAO_AV_Core *av_core)

{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_UDP_Connector::open "));
  this->endpoint_ = endpoint;
  this->av_core_ = av_core;
  int result = this->connector_.open (this,av_core->reactor ());
  return result;
}

int
TAO_AV_UDP_Connector::connect (TAO_FlowSpec_Entry *entry,
                               TAO_AV_Transport *&transport)
{
  this->entry_ = entry;
  ACE_Addr *remote_addr = entry->address ();
  ACE_INET_Addr *local_addr;
  ACE_NEW_RETURN (local_addr,
                  ACE_INET_Addr ("0"),
                  -1);
  int result = this->connector_.connect (this->handler_,
                                         *remote_addr,
                                         *local_addr);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_UDP_connector::open failed\n"),-1);
  // XXX:hack to set the ip address correctly.
  local_addr->set (local_addr->get_port_number (),
                  local_addr->get_host_name ());
  char buf[BUFSIZ];
  local_addr->addr_to_string (buf,BUFSIZ);
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_UDP_CONNECTOR::connect:%s \n",buf));
  entry->set_local_addr (local_addr);
  entry->handler (this->handler_);
  transport = this->handler_->transport ();
  return 0;
}

int
TAO_AV_UDP_Connector::close (void)
{
  return 0;
}

//------------------------------------------------------------
// TAO_AV_UDP_Protocol_Factory
//------------------------------------------------------------

TAO_AV_UDP_Protocol_Factory::TAO_AV_UDP_Protocol_Factory (void)
{
}

TAO_AV_UDP_Protocol_Factory::~TAO_AV_UDP_Protocol_Factory (void)
{
}

int
TAO_AV_UDP_Protocol_Factory::match_protocol (TAO_AV_Core::Protocol protocol)
{
  return (protocol == TAO_AV_Core::TAO_AV_UDP);
}

TAO_AV_Acceptor*
TAO_AV_UDP_Protocol_Factory::make_acceptor (void)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_UDP_Protocol_Factory::make_acceptor "));
  TAO_AV_Acceptor *acceptor = 0;
  ACE_NEW_RETURN (acceptor,
                  TAO_AV_UDP_Acceptor,
                  0);
  return acceptor;
}

TAO_AV_Connector*
TAO_AV_UDP_Protocol_Factory::make_connector (void)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_UDP_Protocol_Factory::make_connector "));
  TAO_AV_Connector *connector = 0;
  ACE_NEW_RETURN (connector,
                  TAO_AV_UDP_Connector,
                  0);
  return connector;
}
//------------------------------------------------------------
// TAO_AV_TCP_Protocol_Factory
//------------------------------------------------------------

TAO_AV_TCP_Protocol_Factory::TAO_AV_TCP_Protocol_Factory (void)
{
}

TAO_AV_TCP_Protocol_Factory::~TAO_AV_TCP_Protocol_Factory (void)
{
}

int
TAO_AV_TCP_Protocol_Factory::match_protocol (TAO_AV_Core::Protocol protocol)
{
  return (protocol == TAO_AV_Core::TAO_AV_TCP);
}

TAO_AV_Acceptor*
TAO_AV_TCP_Protocol_Factory::make_acceptor (void)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_TCP_Protocol_Factory::make_acceptor "));
  TAO_AV_Acceptor *acceptor = 0;
  ACE_NEW_RETURN (acceptor,
                  TAO_AV_TCP_Acceptor,
                  0);
  return acceptor;
}

TAO_AV_Connector*
TAO_AV_TCP_Protocol_Factory::make_connector (void)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_TCP_Protocol_Factory::make_connector "));
  TAO_AV_Connector *connector = 0;
  ACE_NEW_RETURN (connector,
                  TAO_AV_TCP_Connector,
                  0);
  return connector;
}

//------------------------------------------------------------
// TAO_AV_TCP_Base_Connector
//------------------------------------------------------------

int
TAO_AV_TCP_Base_Connector::connector_open (TAO_AV_TCP_Connector *connector,
                                           ACE_Reactor *reactor)
{
  this->connector_ = connector;
  this->reactor_ = reactor;

  int result = ACE_Connector <TAO_AV_TCP_Flow_Handler,ACE_SOCK_CONNECTOR>::open (reactor);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_TCP_Base_Connector::open failed\n"),-1);
  return 0;
}

int
TAO_AV_TCP_Base_Connector::make_svc_handler (TAO_AV_TCP_Flow_Handler *&tcp_handler)
{
  int result =
    this->connector_->make_svc_handler (tcp_handler);
  if (result < 0)
    return result;
  tcp_handler->reactor (this->reactor_);
  return 0;
}

int
TAO_AV_TCP_Base_Connector::connector_connect (TAO_AV_TCP_Flow_Handler *&handler,
                                              const ACE_INET_Addr &remote_addr)
{
  int result = ACE_Connector <TAO_AV_TCP_Flow_Handler,ACE_SOCK_CONNECTOR>::connect (handler,
                                                                                    remote_addr);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_TCP_Base_Connector::connect failed\n"),-1);
  return 0;
}

//------------------------------------------------------------
// TAO_AV_TCP_Connector
//------------------------------------------------------------
TAO_AV_TCP_Connector::TAO_AV_TCP_Connector (void)
{
}

TAO_AV_TCP_Connector::~TAO_AV_TCP_Connector (void)
{
}

int
TAO_AV_TCP_Connector::make_svc_handler (TAO_AV_TCP_Flow_Handler *&tcp_handler)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_TCP_Connector::make_svc_handler\n"));
  TAO_AV_Callback *callback = 0;
  if (this->endpoint_ != 0)
    {
      this->endpoint_->get_callback (this->flowname_.c_str (),
                                     callback);
      ACE_NEW_RETURN (tcp_handler,
                      TAO_AV_TCP_Flow_Handler (callback),
                      -1);
      callback->transport (tcp_handler->transport ());
      TAO_AV_Protocol_Object *object =0;
      ACE_NEW_RETURN (object,
                      TAO_AV_TCP_Object (callback,
                                         tcp_handler->transport ()),
                      -1);
      tcp_handler->protocol_object (object);
      callback->protocol_object (object);
      this->endpoint_->set_protocol_object (this->flowname_.c_str (),
                                            object);
      this->endpoint_->set_handler (this->flowname_.c_str (),tcp_handler);
      this->entry_->protocol_object (object);
      this->entry_->handler (tcp_handler);
    }
  return 0;
}

int
TAO_AV_TCP_Connector::open (TAO_Base_StreamEndPoint *endpoint,
                            TAO_AV_Core *av_core)

{
  this->endpoint_ = endpoint;
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_TCP_Connector::open "));
  int result = this->connector_.connector_open(this,
                                               av_core->reactor ());
  return result;
}

int
TAO_AV_TCP_Connector::connect (TAO_FlowSpec_Entry *entry,
                               TAO_AV_Transport *&transport)
{
  this->entry_ = entry;
  ACE_Addr *remote_addr = entry->address ();
  ACE_INET_Addr *inet_addr = ACE_dynamic_cast (ACE_INET_Addr *,remote_addr);
  TAO_AV_TCP_Flow_Handler *handler;
  int result = this->connector_.connector_connect (handler,
                                                   *inet_addr);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_TCP_connector::open failed\n"),-1);
  entry->handler (handler);
  transport = handler->transport ();
  return 0;
}

int
TAO_AV_TCP_Connector::close (void)
{
  return 0;
}

//------------------------------------------------------------
// TAO_AV_TCP_Base_Connector
//------------------------------------------------------------

int
TAO_AV_TCP_Base_Acceptor::open (TAO_AV_TCP_Acceptor *acceptor,
                                ACE_Reactor *reactor,
                                const ACE_INET_Addr &local_addr,
                                TAO_FlowSpec_Entry *entry)
{
  this->acceptor_ = acceptor;
  this->reactor_ = reactor;
  this->entry_ = entry;

  int result = ACE_Acceptor <TAO_AV_TCP_Flow_Handler,ACE_SOCK_ACCEPTOR>::open (local_addr,reactor);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_TCP_Base_Connector::open failed\n"),-1);
  return 0;
}

int
TAO_AV_TCP_Base_Acceptor::make_svc_handler (TAO_AV_TCP_Flow_Handler *&handler)
{
  int result = this->acceptor_->make_svc_handler (handler);
  if (result < 0)
    return result;
  handler->reactor (this->reactor_);
  this->entry_->handler (handler);
  return 0;
}


//------------------------------------------------------------
// TAO_AV_TCP_Acceptor
//------------------------------------------------------------

TAO_AV_TCP_Acceptor::TAO_AV_TCP_Acceptor (void)
{
}

TAO_AV_TCP_Acceptor::~TAO_AV_TCP_Acceptor (void)
{
}

int
TAO_AV_TCP_Acceptor::make_svc_handler (TAO_AV_TCP_Flow_Handler *&tcp_handler)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_TCP_Acceptor::make_svc_handler\n"));
  TAO_AV_Callback *callback = 0;
  if (this->endpoint_ != 0)
    {
      this->endpoint_->get_callback (this->flowname_.c_str (),
                                     callback);
      ACE_NEW_RETURN (tcp_handler,
                      TAO_AV_TCP_Flow_Handler (callback),
                      -1);
      callback->transport (tcp_handler->transport ());
      TAO_AV_Protocol_Object *object =0;
      ACE_NEW_RETURN (object,
                      TAO_AV_TCP_Object (callback,
                                         tcp_handler->transport ()),
                      -1);
      tcp_handler->protocol_object (object);
      callback->protocol_object (object);
      this->endpoint_->set_protocol_object (this->flowname_.c_str (),
                                         object);
      this->endpoint_->set_handler (this->flowname_.c_str (),tcp_handler);
      this->entry_->protocol_object (object);      
      this->entry_->handler (tcp_handler);
    }
  return 0;
}

int
TAO_AV_TCP_Acceptor::open (TAO_Base_StreamEndPoint *endpoint,
                           TAO_AV_Core *av_core,
                           TAO_FlowSpec_Entry *entry)
{
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_TCP_Acceptor::open "));
  this->av_core_ = av_core;
  this->endpoint_ = endpoint;
  this->entry_ = entry;
  this->flowname_ = entry->flowname ();
  ACE_Addr *address = entry->address ();
  ACE_INET_Addr *inet_addr = (ACE_INET_Addr *) address;
  inet_addr->set (inet_addr->get_port_number (),
                  inet_addr->get_host_name ());
  char buf[BUFSIZ];
  inet_addr->addr_to_string (buf,
                             BUFSIZ);
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_TCP_Acceptor::open: %s",
              buf));
  int result = this->acceptor_.open (this,
                                     av_core->reactor (),
                                     *inet_addr,
                                     entry);
  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_TCP_Acceptor::open failed"),-1);
  entry->set_local_addr (address);
  return 0;
}

int
TAO_AV_TCP_Acceptor::open_default (TAO_Base_StreamEndPoint *endpoint,
                                   TAO_AV_Core *av_core,
                                   TAO_FlowSpec_Entry *entry)
{
  this->av_core_ = av_core;
  this->endpoint_ = endpoint;
  this->entry_ = entry;
  this->flowname_ = entry->flowname ();
  ACE_INET_Addr *address;
  ACE_NEW_RETURN (address,
                  ACE_INET_Addr ("0"),
                  -1);
  int result = this->acceptor_.open (this,
                                     av_core->reactor (),
                                     *address,
                                     entry);

  if (result < 0)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_TCP_Acceptor::open failed"),-1);
  this->acceptor_.acceptor ().get_local_addr (*address);
  address->set (address->get_port_number (),
                address->get_host_name ());
  char buf[BUFSIZ];
  address->addr_to_string (buf,BUFSIZ);
  if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,"TAO_AV_TCP_Acceptor::open_default: %s\n",buf));
  entry->set_local_addr (address);
  return 0;
}


int
TAO_AV_TCP_Acceptor::close (void)
{
  return 0;
}

//------------------------------------------------------------
// TAO_AV_TCP_Flow_Handler
//------------------------------------------------------------

TAO_AV_TCP_Flow_Handler::TAO_AV_TCP_Flow_Handler (TAO_AV_Callback *callback)
  :TAO_AV_Flow_Handler (callback)
{
  ACE_NEW (this->transport_,
           TAO_AV_TCP_Transport (this));
}

TAO_AV_Transport *
TAO_AV_TCP_Flow_Handler::transport (void)
{
  return this->transport_;
}

int
TAO_AV_TCP_Flow_Handler::open (void *arg)
{

  int nodelay = 1;

#if defined (TCP_NODELAY)
  if (this->peer ().set_option (IPPROTO_TCP,
                                TCP_NODELAY,
                                (void *) &nodelay,
                                sizeof (nodelay)) == -1)
    ACE_ERROR_RETURN ((LM_ERROR,
                       "NODELAY failed\n"),
                      -1);
#endif /* TCP_NODELAY */

  // Called by the <Strategy_Acceptor> when the handler is completely
  // connected.
  ACE_INET_Addr addr;

  if (this->peer ().get_remote_addr (addr) == -1)
    return -1;

  char server[MAXHOSTNAMELEN + 16];

  (void) addr.addr_to_string (server, sizeof (server));

  if (TAO_debug_level > 0)
    if (TAO_debug_level > 0) ACE_DEBUG ((LM_DEBUG,
                "(%P|%t) connection to server <%s> on %d\n",
                server, this->peer ().get_handle ()));

  this->peer ().enable (ACE_NONBLOCK);
  // Register the handler with the reactor.
  if (this->reactor ()
      && this->reactor ()->register_handler
      (this,
       ACE_Event_Handler::READ_MASK) == -1)
    ACE_ERROR_RETURN ((LM_ERROR,
                       ASYS_TEXT ("%p\n"),
                       ASYS_TEXT ("unable to register client handler")),
                      -1);
}

int
TAO_AV_TCP_Flow_Handler::handle_input (ACE_HANDLE /*fd*/)
{
  size_t size = BUFSIZ;
  ACE_Message_Block *frame = 0;
  ACE_NEW_RETURN (frame,
                  ACE_Message_Block (size),
                  -1);
  int n = this->transport_->recv (frame->rd_ptr (),
                                  size);
  if (n == -1)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_TCP_Flow_Handler::handle_input recv failed\n"),-1);
  if (n == 0)
    ACE_ERROR_RETURN ((LM_ERROR,"TAO_AV_TCP_Flow_Handler::handle_input connection closed\n"),-1);
  frame->wr_ptr (n);
  this->callback_->receive_frame (frame);
  return 0;
}

int
TAO_AV_TCP_Flow_Handler::handle_timeout (const ACE_Time_Value &tv, 
                                               const void *arg)
{
  return TAO_AV_Flow_Handler::handle_timeout (tv,arg);
}

#if defined (ACE_HAS_EXPLICIT_TEMPLATE_INSTANTIATION)
template class ACE_Node <TAO_AV_Connector*>;
template class ACE_Node <TAO_AV_Acceptor*>;
template class ACE_Unbounded_Set<TAO_AV_Acceptor*>;
template class ACE_Unbounded_Set<TAO_AV_Connector*>;
template class ACE_Unbounded_Set_Iterator<TAO_AV_Acceptor*>;
template class ACE_Unbounded_Set_Iterator<TAO_AV_Connector*>;
template class ACE_Singleton<TAO_AV_Core,ACE_Null_Mutex>;
template class ACE_Acceptor<TAO_AV_TCP_Flow_Handler, ACE_SOCK_ACCEPTOR>;
template class ACE_Connector<TAO_AV_TCP_Flow_Handler, ACE_SOCK_CONNECTOR>;
#elif defined (ACE_HAS_TEMPLATE_INSTANTIATION_PRAGMA)
#pragma instantiate ACE_Node <TAO_AV_Connector*>
#pragma instantiate ACE_Node <TAO_AV_Acceptor*>
#pragma instantiate ACE_Unbounded_Set<TAO_AV_Connector*>
#pragma instantiate ACE_Unbounded_Set<TAO_AV_Acceptor*>
#pragma instantiate ACE_Unbounded_Set_Iterator<TAO_AV_Connector*>
#pragma instantiate ACE_Unbounded_Set_Iterator<TAO_AV_Acceptor*>
#pragma instantiate ACE_Singleton<TAO_AV_Core,ACE_Null_Mutex>
#pragma instantiate ACE_Acceptor<TAO_AV_TCP_Flow_Handler, ACE_SOCK_Acceptor, ACE_INET_Addr>
#pragma instantiate ACE_Connector<TAO_AV_TCP_Flow_Handler, ACE_SOCK_Connector, ACE_INET_Addr>
#endif /* ACE_HAS_EXPLICIT_TEMPLATE_INSTANTIATION */
