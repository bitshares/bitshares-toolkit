
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <iterator>

#include <fc/io/fstream.hpp>
#include <fc/io/json.hpp>
#include <fc/io/stdio.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/rpc/cli.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/crypto/aes.hpp>

#include <bts/app/api.hpp>
#include <bts/chain/address.hpp>
#include <bts/chain/asset_object.hpp>
#include <bts/utilities/key_conversion.hpp>
#include <bts/wallet/wallet.hpp>

#ifndef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#endif

namespace bts { namespace wallet {

namespace detail {

class wallet_api_impl
{
   public:
      wallet_api& self;
      wallet_api_impl( wallet_api& s, fc::api<login_api> rapi );
      virtual ~wallet_api_impl();

      // methods that start with underscore are not incuded in API
      void _resync();
      void _resync_loop();
      void _start_resync_loop();
      void _enable_umask_protection();
      void _disable_umask_protection();

      bool copy_wallet_file( string destination_filename );

      signed_transaction create_account_with_brain_key(string brain_key,
         string account_name,
         string registrar_account,
         string referrer_account,
         bool broadcast = false
         );

      global_property_object            get_global_properties() const;
      dynamic_global_property_object    get_dynamic_global_properties() const;
      account_object                    get_account( string account_name_or_id ) const;
      account_id_type                   get_account_id( string account_name_or_id ) const;
      optional<asset_object>            get_asset( string asset_symbol_or_id )const;
      asset_id_type                     get_asset_id( string asset_name_or_id ) const;
      string                            get_wallet_filename() const;
      fc::ecc::private_key              get_private_key( key_id_type id )const;
      fc::ecc::public_key               get_public_key( key_id_type id )const;

      bool import_key( string account_name_or_id, string wif_key );
      bool load_wallet_file( string wallet_filename = "" );
      void save_wallet_file( string wallet_filename = "" );

      signed_transaction create_asset( string issuer, 
                                       string symbol, 
                                       uint8_t precision, 
                                       asset_object::asset_options common,
                                       fc::optional<asset_object::bitasset_options> bitasset_opts,
                                       bool broadcast = false );

      signed_transaction sign_transaction(
         signed_transaction tx,
         bool broadcast = false
         );

      signed_transaction transfer(
         string from,
         string to,
         uint64_t amount,
         string asset_symbol,
         string memo,
         bool broadcast = false
         );

      signed_transaction issue_asset( uint64_t amount, 
                                      string symbol, 
                                      string to_account,
                                      string memo,
                                      bool broadcast = false );

      std::map<string,std::function<string(fc::variant,const fc::variants&)> >
      _get_result_formatters() const;

      string                  _wallet_filename;
      wallet_data             _wallet;

      map<key_id_type,string> _keys;
      fc::sha512              _checksum;

      fc::api<login_api>      _remote_api;
      fc::api<database_api>   _remote_db;
      fc::api<network_api>    _remote_net;

      fc::future<void>        _resync_loop_task;

      mode_t                  _old_umask;
      const string _wallet_filename_extension = ".wallet";
};

struct help_visitor
{
   help_visitor( std::stringstream& s ):ss(s){}
   std::stringstream& ss;
   template<typename R, typename... Args>
   void operator()( const char* name, std::function<R(Args...)>& memb )const {
      ss << std::setw(40) << std::left << fc::get_typename<R>::name() << " " << name << "( ";
      vector<string> args{ fc::get_typename<typename std::decay<Args>::type>::name()... };
      for( uint32_t i = 0; i < args.size(); ++i )
         ss << args[i] << (i==args.size()-1?" ":", ");
      ss << ")\n";
   }
};

// BLOCK  TRX  OP  VOP
struct operation_printer
{
   operation_result _result;
   operation_printer( const operation_result& r = operation_result() ):_result(r){}
   typedef void result_type;
   template<typename T>
   void operator()( const T& op )const
   {
      balance_accumulator acc;
      op.get_balance_delta( acc, _result );
      std::cerr << fc::get_typename<T>::name() <<" ";
      std::cerr << "balance delta: " << fc::json::to_string(acc.balance) <<"   ";
      std::cerr << fc::json::to_string(op.fee_payer()) << "  fee: " << fc::json::to_string(op.fee);
   }
   void operator()( const account_create_operation& op )const
   {
      balance_accumulator acc;
      op.get_balance_delta( acc, _result );
      std::cerr << "Create Account '" << op.name << "' ";
      std::cerr << "balance delta: " << fc::json::to_string(acc.balance) <<"   ";
      std::cerr << fc::json::to_string(op.fee_payer()) << "  fee: " << fc::json::to_string(op.fee);
   }
};

template< class T >
optional< T > maybe_id( const string& name_or_id )
{
   if( std::isdigit( name_or_id.front() ) )
      return fc::variant(name_or_id).as<T>();
   return optional<T>();
}

string address_to_shorthash( const address& addr )
{
   uint32_t x = addr.addr._hash[0];
   static const char hd[] = "0123456789abcdef";
   string result;

   result += hd[(x >> 0x1c) & 0x0f];
   result += hd[(x >> 0x18) & 0x0f];
   result += hd[(x >> 0x14) & 0x0f];
   result += hd[(x >> 0x10) & 0x0f];
   result += hd[(x >> 0x0c) & 0x0f];
   result += hd[(x >> 0x08) & 0x0f];
   result += hd[(x >> 0x04) & 0x0f];
   result += hd[(x        ) & 0x0f];

   return result;
}

fc::ecc::private_key derive_private_key(
   const std::string& prefix_string,
   int sequence_number
   )
{
   std::string sequence_string = std::to_string(sequence_number);
   fc::sha512 h = fc::sha512::hash(prefix_string + " " + sequence_string);
   fc::ecc::private_key derived_key = fc::ecc::private_key::regenerate(fc::sha256::hash(h));
   return derived_key;
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

wallet_api_impl::wallet_api_impl( wallet_api& s, fc::api<login_api> rapi )
   : self(s), _remote_api( rapi )
{
   _remote_db  = _remote_api->database();
   _remote_net = _remote_api->network();
   _remote_db->subscribe_to_objects( [=]( const fc::variant& obj )
                                     {
                                        wdump((obj));
                                     }, {dynamic_global_property_id_type()} );
   return;
}

wallet_api_impl::~wallet_api_impl()
{
   try
   {
      if( _resync_loop_task.valid() )
         _resync_loop_task.cancel_and_wait();
   }
   catch(fc::canceled_exception&)
   {
      //Expected exception. Move along.
   }
   catch(fc::exception& e)
   {
      edump((e.to_detail_string()));
   }
}

void wallet_api_impl::_enable_umask_protection()
{
   _old_umask = umask( S_IRWXG | S_IRWXO );
   return;
}

void wallet_api_impl::_disable_umask_protection()
{
   umask( _old_umask );
   return;
}

void wallet_api_impl::_start_resync_loop()
{
   _resync_loop_task = fc::async( [this](){ _resync_loop(); }, "cli_wallet resync loop" );
   return;
}

void wallet_api_impl::_resync_loop()
{
   // TODO:  Exception handling
   //    does cancel raise exception?
   _resync();
   fc::microseconds resync_interval = fc::seconds(1);

   _resync_loop_task = fc::schedule( [this](){ _resync_loop(); }, fc::time_point::now() + resync_interval, "cli_wallet resync loop" );
   return;
}

// methods that start with underscore are not incuded in API
void wallet_api_impl::_resync()
{
   // this method is used to update wallet_data annotations
   //   e.g. wallet has been restarted and was not notified
   //   of events while it was down
   //
   // everything that is done "incremental style" when a push
   //   notification is received, should also be done here
   //   "batch style" by querying the blockchain

   if( _wallet.pending_account_registrations.size() > 0 )
   {
      std::vector<string> v_names;
      v_names.reserve( _wallet.pending_account_registrations.size() );

      for( auto it : _wallet.pending_account_registrations )
         v_names.push_back( it.first );

      std::vector< fc::optional< bts::chain::account_object >>
         v_accounts = _remote_db->lookup_account_names( v_names );

      for( fc::optional< bts::chain::account_object > opt_account : v_accounts )
      {
         if( ! opt_account.valid() )
            continue;

         string account_name = opt_account->name;
         auto it = _wallet.pending_account_registrations.find( account_name );
         FC_ASSERT( it != _wallet.pending_account_registrations.end() );
         if( import_key( account_name, it->second ) )
         {
            ilog( "successfully imported account ${name}",
                  ("name", account_name) );
         }
         else
         {
            // somebody else beat our pending registration, there is
            //    nothing we can do except log it and move on
            elog( "account ${name} registered by someone else first!",
                  ("name", account_name) );
            // might as well remove it from pending regs,
            //    because there is now no way this registration
            //    can become valid (even in the extremely rare
            //    possibility of migrating to a fork where the
            //    name is available, the user can always
            //    manually re-register)
         }
         _wallet.pending_account_registrations.erase( it );
      }
   }

   return;
}

std::map<string,std::function<string(fc::variant,const fc::variants&)> >
wallet_api_impl::_get_result_formatters() const
{
   std::map<string,std::function<string(fc::variant,const fc::variants&)> > m;
   m["help"] = []( variant result, const fc::variants& a)
   {
      return result.get_string();
   };

   m["get_account_history"] = []( variant result, const fc::variants& a)
   {
      auto r = result.as<vector<operation_history_object>>();
      for( auto& i : r )
      {
         cerr << i.block_num << " "<<i.trx_in_block << " " << i.op_in_trx << " " << i.virtual_op<< " ";
         i.op.visit( operation_printer() );
         cerr << " \n";
      }
      return string();
   };

   return m;
}

bool wallet_api_impl::copy_wallet_file( string destination_filename )
{
   fc::path src_path = get_wallet_filename();
   if( !fc::exists( src_path ) )
      return false;
   fc::path dest_path = destination_filename + _wallet_filename_extension;
   int suffix = 0;
   while( fc::exists(dest_path) )
   {
      ++suffix;
      dest_path = destination_filename + "-" + to_string( suffix ) + _wallet_filename_extension;
   }
   wlog( "backing up wallet ${src} to ${dest}",
      ("src", src_path)
      ("dest", dest_path) );

   fc::path dest_parent = fc::absolute(dest_path).parent_path();
   try
   {
      _enable_umask_protection();
      if( !fc::exists( dest_parent ) )
         fc::create_directories( dest_parent );
      fc::copy( src_path, dest_path );
   }
   catch(...)
   {
      _disable_umask_protection();
      throw;
   }
   return true;
}

signed_transaction wallet_api_impl::create_account_with_brain_key(
   string brain_key,
   string account_name,
   string registrar_account,
   string referrer_account,
   bool broadcast /* = false */
   )
{ try {
   FC_ASSERT( !self.is_locked() );
   string normalized_brain_key = normalize_brain_key( brain_key );
   // TODO:  scan blockchain for accounts that exist with same brain key
   fc::ecc::private_key owner_privkey = derive_private_key( normalized_brain_key, 0 );
   fc::ecc::private_key active_privkey = derive_private_key( key_to_wif(owner_privkey), 0);

   bts::chain::public_key_type owner_pubkey = owner_privkey.get_public_key();
   bts::chain::public_key_type active_pubkey = active_privkey.get_public_key();

   account_create_operation account_create_op;

   // TODO:  process when pay_from_account is ID

   account_object registrar_account_object =
      this->get_account( registrar_account );

   account_id_type registrar_account_id = registrar_account_object.id;

   account_object referrer_account_object =
         this->get_account( referrer_account );
   account_create_op.referrer = referrer_account_object.id;
   account_create_op.referrer_percent = referrer_account_object.referrer_percent;

   // get pay_from_account_id
   key_create_operation owner_key_create_op;
   owner_key_create_op.fee_paying_account = registrar_account_id;
   owner_key_create_op.key_data = owner_pubkey;

   key_create_operation active_key_create_op;
   active_key_create_op.fee_paying_account = registrar_account_id;
   active_key_create_op.key_data = active_pubkey;

   // key_create_op.calculate_fee(db.current_fee_schedule());

   // TODO:  Check if keys already exist!!!

   relative_key_id_type owner_rkid(0);
   relative_key_id_type active_rkid(1);

   account_create_op.registrar = registrar_account_id;
   account_create_op.name = account_name;
   account_create_op.owner = authority(1, owner_rkid, 1);
   account_create_op.active = authority(1, active_rkid, 1);
   account_create_op.memo_key = active_rkid;

   // current_fee_schedule()
   // find_account(pay_from_account)

   // account_create_op.fee = account_create_op.calculate_fee(db.current_fee_schedule());

   signed_transaction tx;

   tx.operations.push_back( owner_key_create_op );
   tx.operations.push_back( active_key_create_op );
   tx.operations.push_back( account_create_op );

   tx.visit( operation_set_fee( _remote_db->get_global_properties().parameters.current_fees ) );

   vector<key_id_type> paying_keys = registrar_account_object.active.get_keys();

   tx.validate();

   for( key_id_type& key : paying_keys )
   {
      auto it = _keys.find(key);
      if( it != _keys.end() )
      {
         fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
         if( !privkey.valid() )
         {
            FC_ASSERT( false, "Malformed private key in _keys" );
         }
         tx.sign( key, *privkey );
      }
   }

   // we do not insert owner_privkey here because
   //    it is intended to only be used for key recovery
   _wallet.pending_account_registrations[ account_name ] = key_to_wif( active_privkey );
   if( broadcast )
      _remote_net->broadcast_transaction( tx );
   return tx;
} FC_CAPTURE_AND_RETHROW( (account_name)(registrar_account)(referrer_account) ) }

account_object wallet_api_impl::get_account( string account_name_or_id ) const
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

account_id_type wallet_api_impl::get_account_id( string account_name_or_id ) const
{
   FC_ASSERT( account_name_or_id.size() > 0 );
   vector<optional<account_object>> opt_account;
   if( std::isdigit( account_name_or_id.front() ) )
      return fc::variant(account_name_or_id).as<account_id_type>();
   opt_account = _remote_db->lookup_account_names( {account_name_or_id} );
   FC_ASSERT( opt_account.size() && opt_account.front() );
   return opt_account.front()->id;
}

optional<asset_object> wallet_api_impl::get_asset( string asset_symbol_or_id )const
{
   FC_ASSERT( asset_symbol_or_id.size() > 0 );
   vector<optional<asset_object>> opt_asset;
   if( std::isdigit( asset_symbol_or_id.front() ) )
      return _remote_db->get_assets( {fc::variant(asset_symbol_or_id).as<asset_id_type>()}).front();
   opt_asset = _remote_db->lookup_asset_symbols( {asset_symbol_or_id} );
   return opt_asset.front();
}

asset_id_type wallet_api_impl::get_asset_id( string asset_symbol_or_id ) const
{
   FC_ASSERT( asset_symbol_or_id.size() > 0 );
   vector<optional<asset_object>> opt_asset;
   if( std::isdigit( asset_symbol_or_id.front() ) )
      return fc::variant(asset_symbol_or_id).as<asset_id_type>();
   opt_asset = _remote_db->lookup_asset_symbols( {asset_symbol_or_id} );
   FC_ASSERT( (opt_asset.size() > 0) && (opt_asset[0].valid()) );
   return opt_asset[0]->id;
}

string wallet_api_impl::get_wallet_filename() const
{
   return _wallet_filename;
}

bool wallet_api_impl::import_key( string account_name_or_id, string wif_key )
{
   auto opt_priv_key = wif_to_key(wif_key);
   FC_ASSERT( opt_priv_key.valid() );
   bts::chain::address wif_key_address = bts::chain::address(
      opt_priv_key->get_public_key() );

   string shorthash = address_to_shorthash( wif_key_address );

   // backup wallet
   copy_wallet_file( "before-import-key-" + shorthash );
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
   for( const fc::optional<key_object>& opt_key : opt_keys )
   {
      // the requested key ID's should all exist because they are
      //    keys for an account
      FC_ASSERT( opt_key.valid() );
      // we do this check by address because key objects on the
      //    blockchain may not contain a key (i.e. are simply an address)
      if( opt_key->key_address() == wif_key_address )
      {
         _keys[ opt_key->id ] = wif_key;
         save_wallet_file();
         copy_wallet_file( "after-import-key-" + shorthash );
         return true;
      }
   }
   ilog( "key not for account ${name}", ("name",account_name_or_id) );
   return false;
}

bool wallet_api_impl::load_wallet_file( string wallet_filename )
{
   //
   // TODO:  Merge imported wallet with existing wallet,
   //        instead of replacing it
   //
   if( wallet_filename == "" )
      wallet_filename = _wallet_filename;

   if( ! fc::exists( wallet_filename ) )
      return false;

   _wallet = fc::json::from_file( wallet_filename ).as< wallet_data >();
   return true;
}

void wallet_api_impl::save_wallet_file( string wallet_filename )
{
   //
   // Serialize in memory, then save to disk
   //
   // This approach lessens the risk of a partially written wallet
   // if exceptions are thrown in serialization
   //
   
   /** encrypt keys */


   if( wallet_filename == "" )
      wallet_filename = _wallet_filename;

   wlog( "saving wallet to file ${fn}", ("fn", wallet_filename) );

   string data = fc::json::to_pretty_string( _wallet );
   try
   {
      _enable_umask_protection();
      //
      // Parentheses on the following declaration fails to compile,
      // due to the Most Vexing Parse.  Thanks, C++
      //
      // http://en.wikipedia.org/wiki/Most_vexing_parse
      //
      fc::ofstream outfile{ fc::path( wallet_filename ) };
      outfile.write( data.c_str(), data.length() );
      outfile.flush();
      outfile.close();
   }
   catch(...)
   {
      _disable_umask_protection();
      throw;
   }
   return;
}

signed_transaction wallet_api_impl::sign_transaction(
   signed_transaction tx,
   bool broadcast /* = false */
   )
{
   flat_set<account_id_type> req_active_approvals;
   flat_set<account_id_type> req_owner_approvals;

   // awesome hack, dump them both into the same flat_set hehe
   tx.visit( operation_get_required_auths( req_active_approvals, req_owner_approvals ) );

   // TODO:  Only sign if the wallet considers ACCOUNTS to be owned.
   //        Currently the wallet only owns KEYS and will happily sign
   //        for any account...

   // std::merge lets us de-duplicate account_id's that occur in both
   //   sets, and dump them into a vector (as required by remote_db api)
   //   at the same time
   vector< account_id_type > v_approving_account_ids;
   std::merge(req_active_approvals.begin(), req_active_approvals.end(),
              req_owner_approvals.begin() , req_owner_approvals.end(),
              std::back_inserter(v_approving_account_ids));

   vector< optional<account_object> > approving_account_objects =
      _remote_db->get_accounts( v_approving_account_ids );

   FC_ASSERT( approving_account_objects.size() == v_approving_account_ids.size() );

   flat_map< account_id_type, account_object* > approving_account_lut;
   size_t i = 0;
   for( optional<account_object>& approving_acct : approving_account_objects )
   {
      if( !approving_acct.valid() )
      {
         wlog( "operation_get_required_auths said approval of non-existing account ${id} was needed",
            ("id", v_approving_account_ids[i]) );
         i++;
         continue;
      }
      approving_account_lut[ approving_acct->id ] = &(*approving_acct);
      i++;
   }

   flat_set< key_id_type > approving_key_set;
   for( account_id_type& acct_id : req_active_approvals )
   {
      const auto it = approving_account_lut.find( acct_id );
      if( it == approving_account_lut.end() )
         continue;
      const account_object* acct = it->second;
      vector<key_id_type> v_approving_keys = acct->active.get_keys();
      for( const key_id_type& approving_key : v_approving_keys )
         approving_key_set.insert( approving_key );
   }
   for( account_id_type& acct_id : req_owner_approvals )
   {
      const auto it = approving_account_lut.find( acct_id );
      if( it == approving_account_lut.end() )
         continue;
      const account_object* acct = it->second;
      vector<key_id_type> v_approving_keys = acct->owner.get_keys();
      for( const key_id_type& approving_key : v_approving_keys )
         approving_key_set.insert( approving_key );
   }

   for( key_id_type& key : approving_key_set )
   {
      auto it = _keys.find(key);
      if( it != _keys.end() )
      {
         fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
         if( !privkey.valid() )
         {
            FC_ASSERT( false, "Malformed private key in _keys" );
         }
         tx.sign( key, *privkey );
      }
   }

   if( broadcast )
      _remote_net->broadcast_transaction( tx );

   return tx;
}
fc::ecc::public_key  wallet_api_impl::get_public_key( key_id_type id )const
{
   vector<optional<key_object>> keys = _remote_db->get_keys( {id} );
   FC_ASSERT( keys.size() == 1 );
   FC_ASSERT( keys[0].valid() );
   return keys[0]->key();
}
fc::ecc::private_key wallet_api_impl::get_private_key( key_id_type id )const
{
   auto it = _keys.find(id);
   FC_ASSERT( it != _keys.end() );

   fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
   FC_ASSERT( privkey );
   return *privkey;
}

signed_transaction wallet_api_impl::create_asset( string issuer, 
                                       string symbol, 
                                       uint8_t precision, 
                                       asset_object::asset_options common,
                                       fc::optional<asset_object::bitasset_options> bitasset_opts,
                                       bool broadcast  )
{ try {
   account_object issuer_account = get_account( issuer );
   auto current_asset = get_asset(symbol);
   if( current_asset.valid() ) FC_ASSERT( !current_asset, "Symbol already in use.", ("current", *current_asset) );

   asset_create_operation create_op;
   create_op.issuer = issuer_account.id;
   create_op.symbol = symbol;
   create_op.precision = precision;
   create_op.common_options = common;
   create_op.bitasset_options = bitasset_opts;

   static int count = 3;

   signed_transaction tx;
   tx.set_expiration(_remote_db->get_dynamic_global_properties().head_block_id, ++count);
   tx.operations.push_back( create_op );
   tx.visit( operation_set_fee( _remote_db->get_global_properties().parameters.current_fees ) );
   tx.validate();

   return sign_transaction( tx, broadcast );
} FC_CAPTURE_AND_RETHROW( (issuer)(symbol)(precision)(common)(bitasset_opts)(broadcast) ) }


signed_transaction wallet_api_impl::issue_asset( uint64_t amount, string symbol, string to_account, string memo, bool broadcast )
{
   vector< optional< asset_object > > opt_asset = _remote_db->lookup_asset_symbols( {symbol} );
   FC_ASSERT( opt_asset.size() == 1 );
   FC_ASSERT( opt_asset[0].valid() );

   const asset_object& asset_obj = *opt_asset.front();
   account_object to = get_account(to_account);
   account_object issuer = *_remote_db->get_accounts( {asset_obj.id} ).front();

   asset_issue_operation issue_op;
   issue_op.issuer           = asset_obj.issuer;
   issue_op.asset_to_issue   = asset( amount, asset_obj.id );
   issue_op.issue_to_account = to.id;

   if( memo.size() )
   {
      issue_op.memo = memo_data();
      issue_op.memo->from = issuer.memo_key;
      issue_op.memo->to = to.memo_key;
      issue_op.memo->set_message( get_private_key( issuer.memo_key ), 
                     get_public_key( to.memo_key ), memo );
   }
   /** note: this is a hack to generate unique transfers for a performance test */
   static int count = 3;

   signed_transaction tx;
   tx.set_expiration(_remote_db->get_dynamic_global_properties().head_block_id, ++count);
   tx.operations.push_back( issue_op );
   tx.visit( operation_set_fee( _remote_db->get_global_properties().parameters.current_fees ) );
   tx.validate();

   return sign_transaction( tx, broadcast );
}

signed_transaction wallet_api_impl::transfer(
   string from,
   string to,
   uint64_t amount,
   string asset_symbol,
   string memo,
   bool broadcast /* = false */
   )
{
   vector< optional< asset_object > > opt_asset = _remote_db->lookup_asset_symbols( {asset_symbol} );
   FC_ASSERT( opt_asset.size() == 1 );
   FC_ASSERT( opt_asset[0].valid() );

   account_object from_account = get_account( from );
   account_object to_account = get_account( to );
   account_id_type from_id = from_account.id;
   account_id_type to_id = get_account_id( to );

   asset_id_type asset_id = get_asset_id( asset_symbol );

   transfer_operation xfer_op;

   xfer_op.from = from_id;
   xfer_op.to = to_id;
   xfer_op.amount = asset( amount, asset_id );

   if( memo.size() )
   {
      xfer_op.memo = memo_data();
      xfer_op.memo->from = from_account.memo_key;
      xfer_op.memo->to = to_account.memo_key;
      xfer_op.memo->set_message( get_private_key( from_account.memo_key ), 
                     get_public_key( to_account.memo_key ), memo );
   }


   /** note: this is a hack to generate unique transfers for a performance test */
   static int count = 3;

   signed_transaction tx;
   tx.set_expiration(_remote_db->get_dynamic_global_properties().head_block_id, ++count);
   tx.operations.push_back( xfer_op );
   tx.visit( operation_set_fee( _remote_db->get_global_properties().parameters.current_fees ) );
   tx.validate();

   return sign_transaction( tx, broadcast );
}

} // end namespace detail

wallet_api::wallet_api( fc::api<login_api> rapi )
   : my( new detail::wallet_api_impl( *this, rapi ) )
{
   return;
}

wallet_api::~wallet_api()
{
   return;
}

bool wallet_api::copy_wallet_file( string destination_filename )
{
   return my->copy_wallet_file( destination_filename );
}

optional<signed_block> wallet_api::get_block( uint32_t num )
{
   return my->_remote_db->get_block(num);
}

uint64_t wallet_api::get_account_count() const
{
   return my->_remote_db->get_account_count();
}

map<string,account_id_type> wallet_api::list_accounts( const string& lowerbound, uint32_t limit)
{
   return my->_remote_db->lookup_accounts( lowerbound, limit );
}

vector<asset> wallet_api::list_account_balances( const account_id_type& id )
{
   return my->_remote_db->get_account_balances( id, flat_set<asset_id_type>() );
}

vector<asset_object> wallet_api::list_assets( const string& lowerbound, uint32_t limit )const
{
   return my->_remote_db->list_assets( lowerbound, limit );
}

vector<operation_history_object> wallet_api::get_account_history( account_id_type id )const
{
   return my->_remote_db->get_account_history( id, operation_history_id_type() );
}

vector<limit_order_object> wallet_api::get_limit_orders( asset_id_type a, asset_id_type b, uint32_t limit )const
{
   return my->_remote_db->get_limit_orders( a, b, limit );
}

vector<short_order_object> wallet_api::get_short_orders( asset_id_type a, uint32_t limit )const
{
   return my->_remote_db->get_short_orders( a, limit );
}

vector<call_order_object> wallet_api::get_call_orders( asset_id_type a, uint32_t limit )const
{
   return my->_remote_db->get_call_orders( a, limit );
}

vector<force_settlement_object> wallet_api::get_settle_orders( asset_id_type a, uint32_t limit )const
{
   return my->_remote_db->get_settle_orders( a, limit );
}

string wallet_api::suggest_brain_key()const
{
   return string("dummy");
}

string wallet_api::serialize_transaction( signed_transaction tx )const
{
   return my->_remote_api->serialize_transaction( tx, true );
}

variant wallet_api::get_object( object_id_type id ) const
{
   return my->_remote_db->get_objects({id});
}

account_object wallet_api::get_account( string account_name_or_id ) const
{
   return my->get_account( account_name_or_id );
}

account_id_type wallet_api::get_account_id( string account_name_or_id ) const
{
   return my->get_account_id( account_name_or_id );
}

asset_id_type wallet_api::get_asset_id( string asset_symbol_or_id ) const
{
   return my->get_asset_id( asset_symbol_or_id );
}

bool wallet_api::import_key( string account_name_or_id, string wif_key )
{
   return my->import_key( account_name_or_id, wif_key );
}

string wallet_api::normalize_brain_key( string s ) const
{
   return detail::normalize_brain_key( s );
}

fc::ecc::private_key wallet_api::derive_private_key(
   const std::string& prefix_string, int sequence_number) const
{
   return detail::derive_private_key( prefix_string, sequence_number );
}

signed_transaction wallet_api::create_account_with_brain_key(
   string brain_key,
   string account_name,
   string registrar_account,
   string referrer_account,
   bool broadcast /* = false */
   )
{
   return my->create_account_with_brain_key(
      brain_key, account_name, registrar_account,
      referrer_account, broadcast
      );
}
signed_transaction wallet_api::issue_asset( uint64_t amount, 
                                string symbol, 
                                string to_account,
                                string memo,
                                bool broadcast  )
{
   return my->issue_asset( amount, symbol, to_account, memo, broadcast);
}

signed_transaction wallet_api::transfer(
   string from,
   string to,
   uint64_t amount,
   string asset_symbol,
   string memo,
   bool broadcast /* = false */
   )
{
   return my->transfer( from, to, amount, asset_symbol, memo, broadcast );
}
signed_transaction wallet_api::create_asset( string issuer, 
                                             string symbol, 
                                             uint8_t precision, 
                                             asset_object::asset_options common,
                                             fc::optional<asset_object::bitasset_options> bitasset_opts,
                                             bool broadcast )

{
   return my->create_asset( issuer, symbol, precision, common, bitasset_opts, broadcast );
}

void wallet_api::set_wallet_filename( string wallet_filename )
{
   my->_wallet_filename = wallet_filename;
   return;
}

signed_transaction wallet_api::sign_transaction(
   signed_transaction tx,
   bool broadcast /* = false */
   )
{
   return my->sign_transaction( tx, broadcast);
}

global_property_object wallet_api::get_global_properties() const
{
   return my->_remote_db->get_global_properties();
}

dynamic_global_property_object wallet_api::get_dynamic_global_properties() const
{
   return my->_remote_db->get_dynamic_global_properties();
}

string wallet_api::help()const
{
   fc::api<wallet_api> tmp;
   std::stringstream ss;
   tmp->visit( detail::help_visitor(ss) );
   return ss.str();
}

bool wallet_api::load_wallet_file( string wallet_filename )
{
   return my->load_wallet_file( wallet_filename );
}

void wallet_api::save_wallet_file( string wallet_filename )
{
   if( !is_locked() ) lock(); // encrypt it
   my->save_wallet_file( wallet_filename );
}

std::map<string,std::function<string(fc::variant,const fc::variants&)> >
wallet_api::_get_result_formatters() const
{
   return my->_get_result_formatters();
}

void wallet_api::_start_resync_loop()
{
   my->_start_resync_loop();
   return;
}
bool wallet_api::is_locked()const
{
   return my->_checksum == fc::sha512();
}

void wallet_api::lock()
{ try {
   FC_ASSERT( !is_locked() );
   plain_keys data;
   data.keys     = std::move( my->_keys );
   data.checksum = my->_checksum;
   auto plain_txt = fc::raw::pack(data);
   my->_wallet.cipher_keys = fc::aes_encrypt( data.checksum, plain_txt );
   my->_checksum = fc::sha512();
} FC_CAPTURE_AND_RETHROW() }

void wallet_api::unlock( string password )
{ try {
    FC_ASSERT( password.size() > 0  );
    auto pw = fc::sha512::hash( password.c_str(), password.size() );
    vector<char> decrypted = fc::aes_decrypt( pw, my->_wallet.cipher_keys );
    auto pk = fc::raw::unpack<plain_keys>(decrypted);
    FC_ASSERT( pk.checksum == pw );
    my->_keys = std::move(pk.keys);
    my->_checksum = pk.checksum;
} FC_CAPTURE_AND_RETHROW() }

void wallet_api::set_password( string password )
{
    if( my->_wallet.cipher_keys.size() ) 
       FC_ASSERT( !is_locked(), "The wallet must be unlocked before the password can be set" );
    my->_checksum = fc::sha512::hash( password.c_str(), password.size() );
}

} }
