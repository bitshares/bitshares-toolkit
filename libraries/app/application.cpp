#include <bts/app/application.hpp>
#include <bts/app/plugin.hpp>
#include <bts/app/api.hpp>

#include <bts/net/core_messages.hpp>

#include <bts/chain/time.hpp>

#include <bts/utilities/key_conversion.hpp>

#include <fc/rpc/api_connection.hpp>
#include <fc/rpc/websocket_api.hpp>

namespace bts { namespace app {
using net::item_hash_t;
using net::item_id;
using net::message;
using net::block_message;
using net::trx_message;

using chain::block;
using chain::block_id_type;

using std::vector;

namespace detail {

   class application_impl : public net::node_delegate
   {
      void reset_p2p_node(const fc::path& data_dir, const application::daemon_configuration& cfg)
      { try {
         _p2p_network = std::make_shared<net::node>("Graphene Reference Implementation");

         _p2p_network->load_configuration(data_dir / "p2p");
         _p2p_network->set_node_delegate(this);

         for( const fc::ip::endpoint& node : cfg.seed_nodes )
         {
            ilog("Adding seed node ${ip}", ("ip", node));
            _p2p_network->add_node(node);
            _p2p_network->connect_to_endpoint(node);
         }

         _p2p_network->listen_on_port(cfg.p2p_endpoint.port(), true);
         _p2p_network->listen_to_p2p_network();
         ilog("Configured p2p node to listen on ${ip}", ("ip", _p2p_network->get_actual_listening_endpoint()));

         _p2p_network->connect_to_p2p_network();
         _p2p_network->sync_from(net::item_id(net::core_message_type_enum::block_message_type,
                                              _chain_db->head_block_id()),
                                 std::vector<uint32_t>());
      } FC_CAPTURE_AND_RETHROW( (cfg) ) }

      void reset_websocket_server(const application::daemon_configuration& cfg)
      { try {
         _websocket_server = std::make_shared<fc::http::websocket_server>();

         _websocket_server->on_connection([&]( const fc::http::websocket_connection_ptr& c ){
            auto wsc = std::make_shared<fc::rpc::websocket_api_connection>(*c);
            auto login = std::make_shared<bts::app::login_api>( std::ref(*_self) );
            wsc->register_api(fc::api<bts::app::login_api>(login));
            c->set_session_data( wsc );
         });
         _websocket_server->listen( cfg.websocket_endpoint );
         _websocket_server->start_accept();
      } FC_CAPTURE_AND_RETHROW( (cfg) ) }

      void initialize_configuration()
      {
         application::daemon_configuration config;
         _configuration["daemon"] = config;
         save_configuration();
         auto key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));
         if(config.initial_allocation.size() == 1 &&
               config.initial_allocation.front().first.get<public_key_type>() == key.get_public_key())
            dlog("Stake has been allocated to account ${id}, which has private key ${key}.",
                 ("id", account_id_type(11))("key", bts::utilities::key_to_wif(key)));
      }

   public:
      void save_configuration()
      {
         fc::json::save_to_file(_configuration, _data_dir/"config.json");
      }

      application_impl(application* self)
         : _self(self),
           _chain_db(std::make_shared<chain::database>())
      {
      }

      void configure( const fc::path& data_dir)
      {
         _data_dir = data_dir;
         if( fc::exists(data_dir / "config.json") )
         {
            try
            {
               _configuration = fc::json::from_file(data_dir / "config.json").as<application::config>();
            }
            catch( fc::exception& e )
            {
               elog("Failed to read config file:\n${e}", ("e", e.to_detail_string()));
               initialize_configuration();
            }
         }
         else
         {
            ilog("Initializing new configuration file.");
            initialize_configuration();
         }

         configure(data_dir, _configuration["daemon"].as<application::daemon_configuration>());
      }

      void configure( const fc::path& data_dir, const application::daemon_configuration& config )
      {
         _data_dir = data_dir;
         _configuration["daemon"] = config;
         _chain_db->open(data_dir / "blockchain", config.initial_allocation);

         for( const auto& p : _plugins )
            p.second->init();

         reset_p2p_node(data_dir, config);
         reset_websocket_server(config);
      }

      void apply_configuration()
      { try {
         save_configuration();

         auto daemon_config = _configuration["daemon"].as<application::daemon_configuration>();
         reset_p2p_node(_data_dir, daemon_config);
         reset_websocket_server(daemon_config);
      } FC_CAPTURE_AND_RETHROW() }

      /**
       *  If delegate has the item, the network has no need to fetch it.
       */
      virtual bool has_item( const net::item_id& id ) override
      { try {
          if( id.item_type == bts::net::block_message_type )
          {
             return _chain_db->is_known_block( id.item_hash );
          }
          else
          {
             return _chain_db->is_known_transaction( id.item_hash );
          }
      } FC_CAPTURE_AND_RETHROW( (id) ) }

      /**
       *  @brief allows the application to validate an item prior to
       *         broadcasting to peers.
       *
       *  @param sync_mode true if the message was fetched through the sync process, false during normal operation
       *  @returns true if this message caused the blockchain to switch forks, false if it did not
       *
       *  @throws exception if error validating the item, otherwise the item is
       *          safe to broadcast on.
       */
      virtual bool handle_block( const bts::net::block_message& blk_msg, bool sync_mode ) override
      { try {
         ilog("Got block #${n} from network", ("n", blk_msg.block.block_num()));
         try {
            return _chain_db->push_block( blk_msg.block );
         } catch( const fc::exception& e ) {
            elog("Error when pushing block:\n${e}", ("e", e.to_detail_string()));
            throw;
         }
      } FC_CAPTURE_AND_RETHROW( (blk_msg)(sync_mode) ) }

      virtual bool handle_transaction( const bts::net::trx_message& trx_msg, bool sync_mode ) override
      { try {
         ilog("Got transaction from network");
         _chain_db->push_transaction( trx_msg.trx );
         return false;
      } FC_CAPTURE_AND_RETHROW( (trx_msg)(sync_mode) ) }

      /**
       *  Assuming all data elements are ordered in some way, this method should
       *  return up to limit ids that occur *after* the last ID in synopsis that
       *  we recognize.
       *
       *  On return, remaining_item_count will be set to the number of items
       *  in our blockchain after the last item returned in the result,
       *  or 0 if the result contains the last item in the blockchain
       */
      virtual std::vector<item_hash_t> get_item_ids(uint32_t item_type,
                                                    const std::vector<item_hash_t>& blockchain_synopsis,
                                                    uint32_t& remaining_item_count,
                                                    uint32_t limit ) override
      { try {
         FC_ASSERT( item_type == bts::net::block_message_type );
         vector<block_id_type>  result;
         remaining_item_count = 0;
         if( _chain_db->head_block_num() == 0 )
            return result;

         result.reserve(limit);
         block_id_type last_known_block_id;
         auto itr = blockchain_synopsis.rbegin();
         while( itr != blockchain_synopsis.rend() )
         {
            if( _chain_db->is_known_block( *itr ) || *itr == block_id_type() )
            {
               last_known_block_id = *itr;
               break;
            }
            ++itr;
         }

         for( auto num = block::num_from_id(last_known_block_id);
              num <= _chain_db->head_block_num() && result.size() < limit;
              ++num )
            if( num > 0 )
               result.push_back(_chain_db->get_block_id_for_num(num));

         if( block::num_from_id(result.back()) < _chain_db->head_block_num() )
            remaining_item_count = _chain_db->head_block_num() - block::num_from_id(result.back());

         idump((blockchain_synopsis)(limit)(result)(remaining_item_count));
         return result;
      } FC_CAPTURE_AND_RETHROW( (blockchain_synopsis)(remaining_item_count)(limit) ) }

      /**
       *  Given the hash of the requested data, fetch the body.
       */
      virtual message get_item( const item_id& id ) override
      { try {
         ilog("Request for item ${id}", ("id", id));
         if( id.item_type == bts::net::block_message_type )
         {
            auto opt_block = _chain_db->fetch_block_by_id( id.item_hash );
            if( !opt_block )
               elog("Couldn't find block ${id} -- corresponding ID in our chain is ${id2}",
                    ("id", id.item_hash)("id2", _chain_db->get_block_id_for_num(block::num_from_id(id.item_hash))));
            FC_ASSERT( opt_block.valid() );
            ilog("Serving up block #${num}", ("num", opt_block->block_num()));
            return block_message( std::move(*opt_block) );
         }
         return trx_message( _chain_db->get_recent_transaction( id.item_hash ) );
      } FC_CAPTURE_AND_RETHROW( (id) ) }

      virtual fc::sha256 get_chain_id()const override
      {
         return fc::sha256();
      }

      /**
       * Returns a synopsis of the blockchain used for syncing.
       * This consists of a list of selected item hashes from our current preferred
       * blockchain, exponentially falling off into the past.  Horrible explanation.
       *
       * If the blockchain is empty, it will return the empty list.
       * If the blockchain has one block, it will return a list containing just that block.
       * If it contains more than one block:
       *   the first element in the list will be the hash of the genesis block
       *   the second element will be the hash of an item at the half way point in the blockchain
       *   the third will be ~3/4 of the way through the block chain
       *   the fourth will be at ~7/8...
       *     &c.
       *   the last item in the list will be the hash of the most recent block on our preferred chain
       */
      virtual std::vector<item_hash_t> get_blockchain_synopsis( uint32_t item_type,
                                                                const bts::net::item_hash_t& reference_point,
                                                                uint32_t number_of_blocks_after_reference_point ) override
      { try {
         std::vector<item_hash_t> result;
         result.reserve(30);
         auto head_block_num = _chain_db->head_block_num();
         result.push_back( _chain_db->head_block_id() );
         auto current = 1;
         while( current < head_block_num )
         {
            result.push_back( _chain_db->get_block_id_for_num( head_block_num - current ) );
            current = current*2;
         }
         std::reverse( result.begin(), result.end() );
         idump((reference_point)(number_of_blocks_after_reference_point)(result));
         return result;
      } FC_CAPTURE_AND_RETHROW( (reference_point)(number_of_blocks_after_reference_point) ) }

      /**
       *  Call this after the call to handle_message succeeds.
       *
       *  @param item_type the type of the item we're synchronizing, will be the same as item passed to the sync_from() call
       *  @param item_count the number of items known to the node that haven't been sent to handle_item() yet.
       *                    After `item_count` more calls to handle_item(), the node will be in sync
       */
      virtual void     sync_status( uint32_t item_type, uint32_t item_count ) override
      {
         // any status reports to GUI go here
      }

      /**
       *  Call any time the number of connected peers changes.
       */
      virtual void     connection_count_changed( uint32_t c ) override
      {
        // any status reports to GUI go here
      }

      virtual uint32_t get_block_number(const item_hash_t& block_id) override
      { try {
         return block::num_from_id(block_id);
      } FC_CAPTURE_AND_RETHROW( (block_id) ) }

      /**
       * Returns the time a block was produced (if block_id = 0, returns genesis time).
       * If we don't know about the block, returns time_point_sec::min()
       */
      virtual fc::time_point_sec get_block_time(const item_hash_t& block_id) override
      { try {
         auto opt_block = _chain_db->fetch_block_by_id( block_id );
         if( opt_block.valid() ) return opt_block->timestamp;
         return fc::time_point_sec::min();
      } FC_CAPTURE_AND_RETHROW( (block_id) ) }

      /** returns bts::blockchain::now() */
      virtual fc::time_point_sec get_blockchain_now() override
      {
         return bts::chain::now();
      }

      virtual item_hash_t get_head_block_id() const override
      {
         return _chain_db->head_block_id();
      }

      virtual uint32_t estimate_last_known_fork_from_git_revision_timestamp(uint32_t unix_timestamp) const override
      {
         return 0; // there are no forks in graphene
      }

      virtual void error_encountered(const std::string& message, const fc::oexception& error) override
      {
         // notify GUI or something cool
      }

      application* _self;

      fc::path _data_dir;

      std::shared_ptr<bts::chain::database>        _chain_db;
      std::shared_ptr<bts::net::node>              _p2p_network;
      std::shared_ptr<fc::http::websocket_server>  _websocket_server;
      application::config                          _configuration;

      std::map<string, std::shared_ptr<abstract_plugin>> _plugins;
   };

}

application::application()
   : my(new detail::application_impl(this))
{}

application::~application()
{
   if( my->_p2p_network )
   {
      ilog("Closing p2p node");
      my->_p2p_network->close();
   }
   if( my->_chain_db )
   {
      ilog("Closing chain database");
      my->_chain_db->close();
   }
}

void application::configure( const fc::path& data_dir )
{
   my->configure( data_dir );
}

void application::configure(const fc::path& data_dir, const application::daemon_configuration& config)
{
   my->configure(data_dir, config);
}

std::shared_ptr<abstract_plugin> application::get_plugin(const string& name) const
{
   return my->_plugins[name];
}

application::config& application::configuration()
{
   return my->_configuration;
}

const application::config& application::configuration() const
{
   return my->_configuration;
}

void application::apply_configuration()
{
   my->apply_configuration();
}

void application::save_configuration() const
{
   my->save_configuration();
}

net::node_ptr application::p2p_node()
{
   return my->_p2p_network;
}

std::shared_ptr<chain::database> application::chain_database() const
{
   return my->_chain_db;
}

void bts::app::application::add_plugin(const string& name, std::shared_ptr<bts::app::abstract_plugin> p)
{
   my->_plugins[name] = p;
}

// namespace detail
} }
