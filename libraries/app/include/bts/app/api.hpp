#pragma once
#include <bts/chain/types.hpp>
#include <bts/chain/database.hpp>
#include <bts/chain/account_object.hpp>
#include <bts/chain/asset_object.hpp>
#include <bts/chain/key_object.hpp>
#include <bts/net/node.hpp>
#include <fc/api.hpp>

namespace bts { namespace app {
   using namespace bts::chain;

   class application;

   class database_api
   {
      public:
         database_api( bts::chain::database& db );
         fc::variants                      get_objects( const vector<object_id_type>& ids )const;
         optional<signed_block>            get_block( uint32_t block_num )const;
         global_property_object            get_global_properties()const;
         dynamic_global_property_object    get_dynamic_global_properties()const;
         vector<optional<key_object>>      get_keys( const vector<key_id_type>& key_ids )const;
         vector<optional<account_object>>  get_accounts( const vector<account_id_type>& account_ids )const;
         vector<optional<asset_object>>    get_assets( const vector<asset_id_type>& asset_ids )const;

         vector<optional<account_object>>  lookup_account_names( const vector<string>& account_name )const;
         vector<optional<asset_object>>    lookup_asset_symbols( const vector<string>& asset_symbols )const;

         bts::chain::database& _db;
   };

   class history_api
   {
        history_api( application& app ):_app(app){}

        /**
         *  @return all operations related to account id from the most recent until, but not including limit_id
         */
        vector<operation_history_object>  get_account_history( account_id_type id, operation_history_id_type limit_id  = operation_history_id_type() )const;

        application&              _app;
   };

   class network_api
   {
      public:
         network_api( application& a ):_app(a){}

         void                           broadcast_transaction( const signed_transaction& trx );
         void                           add_node( const fc::ip::endpoint& ep );
         std::vector<net::peer_status>  get_connected_peers() const;

         application&              _app;
   };

   class login_api
   {
      public:
         login_api( application& a ):_app(a){}

         bool                   login( const string& user, const string& password );
         fc::api<network_api>   network()const;
         fc::api<database_api>  database()const;
         signed_transaction     sign_transaction( signed_transaction trx, const vector< string >& wif_keys )const;

         application&                      _app;
         optional< fc::api<database_api> > _database_api;
         optional< fc::api<network_api> >  _network_api;
   };

}}  // bts::app

FC_API( bts::app::database_api, 
        (get_objects)
        (get_block)
        (get_global_properties)
        (get_dynamic_global_properties)
        (get_keys)
        (get_accounts)
        (get_assets)
        (lookup_account_names)
        (lookup_asset_symbols) 
     )
FC_API( bts::app::network_api, (broadcast_transaction)(add_node)(get_connected_peers) )
FC_API( bts::app::login_api, (login)(network)(database) )
