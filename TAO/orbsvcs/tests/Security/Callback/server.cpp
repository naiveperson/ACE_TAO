#include "ace/Log_Msg.h"
#include "ace/streams.h"
#include "server_i.h"


ACE_RCSID (Callback,
           server,
           "$Id$")


int
main (int argc, char *argv[])
{
  ACE_DECLARE_NEW_CORBA_ENV;
  ACE_TRY
    {
      //
      // Initialize the ORB
      //
      CORBA::ORB_var orb =
        CORBA::ORB_init (argc, argv, "" TAO_ENV_ARG_PARAMETER);
      ACE_TRY_CHECK;

      //
      // Get the Root POA.
      //
      CORBA::Object_var obj =
        orb->resolve_initial_references ("RootPOA" TAO_ENV_ARG_PARAMETER);
      ACE_TRY_CHECK;

      PortableServer::POA_var poa =
        PortableServer::POA::_narrow (obj.in () TAO_ENV_ARG_PARAMETER);
      ACE_TRY_CHECK;

      //
      // Create the server, get object reference,
      // and create "stringified" IOR.
      //
      server_i *theServer = 0;
      ACE_NEW_RETURN (theServer,
                      server_i (orb.in ()),
                      -1);

      PortableServer::ObjectId_var oid =
        poa->activate_object (theServer TAO_ENV_ARG_PARAMETER);
      ACE_TRY_CHECK;
      CORBA::Object_var server_obj =
        poa->id_to_reference (oid.in() TAO_ENV_ARG_PARAMETER);
      ACE_TRY_CHECK;
      CORBA::String_var server_IORString =
        orb->object_to_string (server_obj TAO_ENV_ARG_PARAMETER);
      ACE_TRY_CHECK;

      //
      // Write the IOR to a file.
      //
      ofstream server_IOR_file ("server.ior");
      server_IOR_file << server_IORString.in () << endl;

      //
      // Activate the POA manager.
      //
      PortableServer::POAManager_var mgr =
        poa->the_POAManager (TAO_ENV_SINGLE_ARG_PARAMETER);
      ACE_TRY_CHECK;
      mgr->activate (TAO_ENV_SINGLE_ARG_PARAMETER);
      ACE_TRY_CHECK;

      ACE_DEBUG ((LM_INFO,
                  ACE_TEXT ("(%P) calling orb->run () ...\n")));

      orb->run (TAO_ENV_SINGLE_ARG_PARAMETER);
      ACE_TRY_CHECK;

      poa->destroy (1, 1 TAO_ENV_ARG_PARAMETER);
      ACE_TRY_CHECK;

      orb->destroy (TAO_ENV_SINGLE_ARG_PARAMETER);
      ACE_TRY_CHECK;
    }
  ACE_CATCHANY
    {
      ACE_PRINT_EXCEPTION (ACE_ANY_EXCEPTION,
                           ACE_TEXT ("Caught exception\n"));
      return -1;
    }
  ACE_ENDTRY;

   return 0;
}
