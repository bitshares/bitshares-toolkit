#include <bts/app/api.hpp>
#include <bts/chain/address.hpp>
#include <bts/utilities/key_conversion.hpp>
#include <fc/io/json.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/io/stdio.hpp>
#include <iostream>
#include <fc/rpc/cli.hpp>
#include <iomanip>


using namespace bts::app;
using namespace bts::chain;
using namespace bts::utilities;
using namespace std;

struct wallet_data
{
   flat_set<account_id_type> accounts;
   map<key_id_type, string>  keys;
   string                    ws_server = "ws://localhost:8090";
   string                    ws_user;
   string                    ws_password;
};
FC_REFLECT( wallet_data, (accounts)(keys)(ws_server)(ws_user)(ws_password) );


/**
 *  This wallet assumes nothing about where the database server is
 *  located and performs minimal caching.  This API could be provided
 *  locally to be used by a web interface.
 */
class wallet_api
{
   public:
      wallet_api( fc::api<login_api> rapi )
      :_remote_api(rapi)
      {
         _remote_db  = _remote_api->database();
         _remote_net = _remote_api->network();
      }
      string  help()const;

      string  suggest_brain_key()const
      {
        return string("dummy");
      }
      variant get_object( object_id_type id )
      {
         return _remote_db->get_objects({id});
      }
      account_object get_account( string account_name_or_id )
      {
         FC_ASSERT( account_name_or_id.size() > 0 );
         vector<optional<account_object>> opt_account;
         if( std::isdigit( account_name_or_id.front() ) )
            opt_account = _remote_db->get_accounts( {fc::variant(account_name_or_id).as<account_id_type>()} );
         else
            opt_account = _remote_db->lookup_account_names( {account_name_or_id} );
         FC_ASSERT( opt_account.size() && opt_account.front() );
         return *opt_account.front();
      }

      bool import_key( string account_name_or_id, string wif_key )
      {
         auto opt_priv_key = wif_to_key(wif_key);
         FC_ASSERT( opt_priv_key.valid() );
         auto wif_key_address = opt_priv_key->get_public_key();

         auto acnt = get_account( account_name_or_id );

         flat_set<key_id_type> keys;
         for( auto item : acnt.active.auths )
         {
             if( item.first.type() == key_object_type )
                keys.insert( item.first );
         }
         for( auto item : acnt.owner.auths )
         {
             if( item.first.type() == key_object_type )
                keys.insert( item.first );
         }
         auto opt_keys = _remote_db->get_keys( vector<key_id_type>(keys.begin(),keys.end()) );
         for( auto opt_key : opt_keys )
         {
            FC_ASSERT( opt_key.valid() );
            if( opt_key->key_address() == wif_key_address )
            {
               _wallet.keys[ opt_key->id ] = wif_key;
               return true;
            }
         }
         ilog( "key not for account ${name}", ("name",account_name_or_id) );
         return false;
      }

      string normalize_brain_key( string s )
      {
          size_t i = 0, n = s.length();
          std::string result;
          char c;
          result.reserve( n );

          bool preceded_by_whitespace = false;
          bool non_empty = false;
          while( i < n )
          {
              c = s[i++];
              switch( c )
              {
                  case ' ':  case '\t': case '\r': case '\n': case '\v': case '\f':
                      preceded_by_whitespace = true;
                      continue;

                  case 'a': c = 'A'; break;
                  case 'b': c = 'B'; break;
                  case 'c': c = 'C'; break;
                  case 'd': c = 'D'; break;
                  case 'e': c = 'E'; break;
                  case 'f': c = 'F'; break;
                  case 'g': c = 'G'; break;
                  case 'h': c = 'H'; break;
                  case 'i': c = 'I'; break;
                  case 'j': c = 'J'; break;
                  case 'k': c = 'K'; break;
                  case 'l': c = 'L'; break;
                  case 'm': c = 'M'; break;
                  case 'n': c = 'N'; break;
                  case 'o': c = 'O'; break;
                  case 'p': c = 'P'; break;
                  case 'q': c = 'Q'; break;
                  case 'r': c = 'R'; break;
                  case 's': c = 'S'; break;
                  case 't': c = 'T'; break;
                  case 'u': c = 'U'; break;
                  case 'v': c = 'V'; break;
                  case 'w': c = 'W'; break;
                  case 'x': c = 'X'; break;
                  case 'y': c = 'Y'; break;
                  case 'z': c = 'Z'; break;

                  default:
                      break;
              }
              if( preceded_by_whitespace && non_empty )
                  result.push_back(' ');
              result.push_back(c);
              preceded_by_whitespace = false;
              non_empty = true;
          }

          return result;
      }

      fc::ecc::private_key derive_private_key(
          const std::string& prefix_string, int sequence_number)
      {
           std::string sequence_string = std::to_string(sequence_number);
           fc::sha512 h = fc::sha512::hash(prefix_string + " " + sequence_string);
           fc::ecc::private_key derived_key = fc::ecc::private_key::regenerate(fc::sha256::hash(h));
           return derived_key;
      }

      signed_transaction create_account_with_brain_key(
          string brain_key,
          string account_name,
          string pay_from_account
          )
      {
        // TODO:  process when pay_from_account is ID

        account_object pay_from_account_object =
            this->get_account( pay_from_account );

        account_id_type pay_from_account_id = pay_from_account_object.id;

        string normalized_brain_key = normalize_brain_key( brain_key );
        // TODO:  scan blockchain for accounts that exist with same brain key
        fc::ecc::private_key owner_privkey = derive_private_key( normalized_brain_key, 0 );
        fc::ecc::private_key active_privkey = derive_private_key( key_to_wif(owner_privkey), 0);

        bts::chain::public_key_type owner_pubkey = owner_privkey.get_public_key();
        bts::chain::public_key_type active_pubkey = active_privkey.get_public_key();

        // get pay_from_account_id
        key_create_operation owner_key_create_op;
        owner_key_create_op.fee_paying_account = pay_from_account_id;
        owner_key_create_op.key_data = owner_pubkey;

        key_create_operation active_key_create_op;
        active_key_create_op.fee_paying_account = pay_from_account_id;
        active_key_create_op.key_data = active_pubkey;

        // key_create_op.calculate_fee(db.current_fee_schedule());

        // TODO:  Check if keys already exist!!!

        account_create_operation account_create_op;

        vector<string> v_pay_from_account;
        v_pay_from_account.push_back( pay_from_account );

        account_create_op.fee_paying_account = pay_from_account_id;

        relative_key_id_type owner_rkid(0);
        relative_key_id_type active_rkid(1);

        account_create_op.name = account_name;
        account_create_op.owner = authority(1, owner_rkid, 1);
        account_create_op.active = authority(1, active_rkid, 1);
        account_create_op.memo_key = active_rkid;
        account_create_op.voting_key = active_rkid;
        account_create_op.vote = flat_set<vote_tally_id_type>();

        // current_fee_schedule()
        // find_account(pay_from_account)

        // account_create_op.fee = account_create_op.calculate_fee(db.current_fee_schedule());

        signed_transaction tx;

        tx.operations.push_back( owner_key_create_op );
        tx.operations.push_back( active_key_create_op );
        tx.operations.push_back( account_create_op );

        tx.visit( operation_set_fee( _remote_db->get_global_properties().parameters.current_fees ) );

        vector<key_id_type> paying_keys = pay_from_account_object.active.get_keys();

        tx.validate();

        for( key_id_type& key : paying_keys )
        {
            auto it = _wallet.keys.find(key);
            if( it != _wallet.keys.end() )
            {
                fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
                if( !privkey.valid() )
                {
                    FC_ASSERT( false, "Malformed private key in _wallet.keys" );
                }
                tx.sign( *privkey );
            }
        }

        return tx;
      }

      signed_transaction transfer( string from,
                                   string to,
                                   uint64_t amount,
                                   string asset_symbol,
                                   string memo,
                                   bool broadcast = false )
      {
        auto opt_asset = _remote_db->lookup_asset_symbols( {asset_symbol} );
        wdump( (opt_asset) );
        return signed_transaction();
      }

      wallet_data             _wallet;
      fc::api<login_api>      _remote_api;
      fc::api<database_api>   _remote_db;
      fc::api<network_api>    _remote_net;
};

FC_API( wallet_api,
        (help)
        (import_key)
        (suggest_brain_key)
        (create_account_with_brain_key)
        (transfer)
        (get_account)
        (get_object)
        (normalize_brain_key)
       )

struct help_visitor
{
   help_visitor( std::stringstream& s ):ss(s){}
   std::stringstream& ss;
   template<typename R, typename... Args>
   void operator()( const char* name, std::function<R(Args...)>& memb )const {
      ss << std::setw(40) << std::left << fc::get_typename<R>::name() << " " << name << "( ";
      vector<string> args{ fc::get_typename<Args>::name()... };
      for( uint32_t i = 0; i < args.size(); ++i )
         ss << args[i] << (i==args.size()-1?" ":", ");
      ss << ")\n";
   }

};
string  wallet_api::help()const
{
   fc::api<wallet_api> tmp;
   std::stringstream ss;
   tmp->visit( help_visitor(ss) );
   return ss.str();
}

int main( int argc, char** argv )
{
   try {
      FC_ASSERT( argc > 1, "usage: ${cmd} WALLET_FILE", ("cmd",argv[0]) );

      fc::ecc::private_key genesis_private_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")));
      idump( (key_to_wif( genesis_private_key ) ) );
      idump( (account_id_type()) );

      wallet_data wallet;
      fc::path wallet_file(argv[1]);
      if( fc::exists( wallet_file ) )
          wallet = fc::json::from_file( wallet_file ).as<wallet_data>();

      fc::http::websocket_client client;
      auto con  = client.connect( wallet.ws_server );
      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*con);
      con->closed.connect( [&](){ elog( "connection closed" ); } );

      auto remote_api = apic->get_remote_api< login_api >();
      FC_ASSERT( remote_api->login( wallet.ws_user, wallet.ws_password ) );

      auto wapiptr = std::make_shared<wallet_api>(remote_api);
      wapiptr->_wallet = wallet;

      fc::api<wallet_api> wapi(wapiptr);

      auto wallet_cli = std::make_shared<fc::rpc::cli>();
      wallet_cli->format_result( "help", [&]( variant result, const fc::variants& a) {
                                    return result.get_string();
                                });
      wallet_cli->register_api( wapi );
      wallet_cli->start();
      wallet_cli->wait();
   }
   catch ( const fc::exception& e )
   {
      std::cout << e.to_detail_string() << "\n";
   }
   return -1;
}
