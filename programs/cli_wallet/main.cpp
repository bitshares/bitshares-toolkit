
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <iterator>

#include <fc/io/json.hpp>
#include <fc/io/stdio.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/rpc/cli.hpp>
#include <fc/rpc/websocket_api.hpp>

#include <bts/app/api.hpp>
#include <bts/chain/address.hpp>
#include <bts/utilities/key_conversion.hpp>
#include <bts/wallet/wallet.hpp>

#include <fc/rpc/websocket_api.hpp>

#include <fc/interprocess/signals.hpp>
#include <boost/program_options.hpp>

#include <fc/log/file_appender.hpp>
#include <fc/log/logger.hpp>
#include <fc/log/logger_config.hpp>
#ifndef WIN32
#include <csignal>
#endif

using namespace bts::app;
using namespace bts::chain;
using namespace bts::utilities;
using namespace bts::wallet;
using namespace std;
namespace bpo = boost::program_options;

int main( int argc, char** argv )
{
   try {

      wdump( (bts::chain::public_key_type( fc::ecc::private_key::regenerate( fc::sha256::hash(std::string("nathan")) ).get_public_key() ) ) );
      wdump( (bts::utilities::key_to_wif( fc::ecc::private_key::regenerate( fc::sha256::hash(std::string("nathan")) ) ) ) );
      boost::program_options::options_description opts;
         opts.add_options()
         ("help,h", "Print this help message and exit.")
         ("server-rpc-endpoint,s", bpo::value<string>()->implicit_value("ws://127.0.0.1:8090"), "Server websocket RPC endpoint")
         ("server-rpc-user,u", bpo::value<string>(), "Server Username")
         ("server-rpc-password,p", bpo::value<string>(), "Server Password")
         ("rpc-endpoint,r", bpo::value<string>()->implicit_value("127.0.0.1:8091"), "Endpoint for wallet websocket RPC to listen on")
         ("rpc-tls-endpoint,t", bpo::value<string>()->implicit_value("127.0.0.1:8092"), "Endpoint for wallet websocket TLS RPC to listen on")
         ("rpc-tls-certificate,c", bpo::value<string>()->implicit_value("server.pem"), "PEM certificate for wallet websocket TLS RPC")
         ("daemon,d", "Run the wallet in daemon mode" )
         ("wallet-file,w", bpo::value<string>()->implicit_value("wallet.json"), "wallet to load");

      bpo::variables_map options;

      bpo::store( bpo::parse_command_line(argc, argv, opts), options );

      if( options.count("help") )
      {
         std::cout << opts << "\n";
         return 0;
      }

      fc::path data_dir;
      fc::logging_config cfg;
      fc::path log_dir = data_dir / "logs";

      fc::file_appender::config ac;
      ac.filename             = log_dir / "default" / "default.log";
      ac.flush                = true;
      ac.rotate               = true;
      ac.rotation_interval    = fc::hours( 1 );
      ac.rotation_limit       = fc::days( 1 );
      ac.rotation_compression = false;

      std::cout << "Logging to file: " << (data_dir / ac.filename).preferred_string() << "\n";

      fc::file_appender::config ac_p2p;
      ac_p2p.filename             = log_dir / "p2p" / "p2p.log";
      ac_p2p.flush                = true;
      ac_p2p.rotate               = true;
      ac_p2p.rotation_interval    = fc::hours( 1 );
      ac_p2p.rotation_limit       = fc::days( 1 );
      ac_p2p.rotation_compression = false;

      std::cout << "Logging P2P to file: " << (data_dir / ac_p2p.filename).preferred_string() << "\n";

      cfg.appenders.push_back(fc::appender_config( "default", "file", fc::variant(ac)));
      cfg.appenders.push_back(fc::appender_config( "p2p", "file", fc::variant(ac_p2p)));

      fc::configure_logging( cfg );



      fc::ecc::private_key genesis_private_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")));

      idump( (key_to_wif( genesis_private_key ) ) );
      idump( (account_id_type()) );

      fc::ecc::private_key nathan_private_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));
      idump( (key_to_wif( nathan_private_key ) ) );

      //
      // TODO:  We read wallet_data twice, once in main() to grab the
      //    socket info, again in wallet_api when we do
      //    load_wallet_file().  Seems like this could be better
      //    designed.
      //
      wallet_data wdata;
      wdump((options.count("wallet-file")));

      ilog(".");
      fc::path wallet_file( options.count("wallet-file") ? options.at("wallet-file").as<string>() : "wallet.json");
      ilog(".");
      if( fc::exists( wallet_file ) )
          wdata = fc::json::from_file( wallet_file ).as<wallet_data>();

      if( options.count("server-rpc-endpoint") )
         wdata.ws_server = options.at("server-rpc-endpoint").as<std::string>();
      if( options.count("server-rpc-user") )
         wdata.ws_user = options.at("server-rpc-user").as<std::string>();
      if( options.count("server-rpc-password") )
         wdata.ws_password = options.at("server-rpc-password").as<std::string>();

      ilog(".");
      fc::http::websocket_client client;
      auto con  = client.connect( wdata.ws_server );
      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*con);
      //con->closed.connect( [=](){ elog( "connection closed" ); } );

      auto remote_api = apic->get_remote_api< login_api >(1);
      FC_ASSERT( remote_api->login( wdata.ws_user, wdata.ws_password ) );

      auto wapiptr = std::make_shared<wallet_api>(remote_api);
      wapiptr->set_wallet_filename( wallet_file.generic_string() );
      wapiptr->load_wallet_file();
      wapiptr->_start_resync_loop();

      fc::api<wallet_api> wapi(wapiptr);

      auto wallet_cli = std::make_shared<fc::rpc::cli>();
      for( auto& name_formatter : wapiptr->_get_result_formatters() )
         wallet_cli->format_result( name_formatter.first, name_formatter.second );

      if( wapiptr->is_new() )
      {
         std::cout << "Please use the set_password method to initialize a new wallet before continuing\n";
         wallet_cli->set_prompt( "new >>> " );
      }
      else
         wallet_cli->set_prompt( "locked >>> " );
      wapiptr->lock_changed.connect( [&](bool locked){ wallet_cli->set_prompt(  locked ? "locked >>> " : "unlocked >>> " ); } );

      auto _websocket_server = std::make_shared<fc::http::websocket_server>();
      if( options.count("rpc-endpoint") )
      {
         _websocket_server->on_connection([&]( const fc::http::websocket_connection_ptr& c ){
            auto wsc = std::make_shared<fc::rpc::websocket_api_connection>(*c);
//            auto login = std::make_shared<bts::app::login_api>( std::ref(*_self) );
            wsc->register_api(wapi);
            c->set_session_data( wsc );
         });
         ilog( "Listening for incoming RPC requests on ${p}", ("p", options.at("rpc-endpoint").as<string>() ));
         _websocket_server->listen( fc::ip::endpoint::from_string(options.at("rpc-endpoint").as<string>()) );
         _websocket_server->start_accept();
      }

      string cert_pem = "server.pem";
      if( options.count( "rpc-tls-certificate" ) )
         cert_pem = options.at("rpc-tls-certificate").as<string>();

      auto _websocket_tls_server = std::make_shared<fc::http::websocket_tls_server>(cert_pem);
      if( options.count("rpc-tls-endpoint") )
      {
         _websocket_tls_server->on_connection([&]( const fc::http::websocket_connection_ptr& c ){
            auto wsc = std::make_shared<fc::rpc::websocket_api_connection>(*c);
//            auto login = std::make_shared<bts::app::login_api>( std::ref(*_self) );
            wsc->register_api(wapi);
            c->set_session_data( wsc );
         });
         ilog( "Listening for incoming TLS RPC requests on ${p}", ("p", options.at("rpc-tls-endpoint").as<string>() ));
         _websocket_tls_server->listen( fc::ip::endpoint::from_string(options.at("rpc-tls-endpoint").as<string>()) );
         _websocket_tls_server->start_accept();
      }

      fc::promise<int>::ptr exit_promise = new fc::promise<int>("UNIX Signal Handler");
      fc::set_signal_handler([&exit_promise](int signal) {
         exit_promise->set_value(signal);
      }, SIGINT);


      if( !options.count( "daemon" ) )
      {
         wallet_cli->register_api( wapi );
         wallet_cli->start();
         wallet_cli->wait();
      }
      else
      {
        ilog( "Entering Daemon Mode, ^C to exit" );
        exit_promise->wait();
      }

      wapi->save_wallet_file(wallet_file.generic_string());
   }
   catch ( const fc::exception& e )
   {
      std::cout << e.to_detail_string() << "\n";
   }
   return -1;
}
