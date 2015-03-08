#include <bts/chain/database.hpp>
#include <bts/chain/undo_database.hpp>

namespace bts { namespace chain {

void undo_database::enable()  { _disabled = false; }
void undo_database::disable() { _disabled = true; }

undo_database::session undo_database::start_undo_session()
{
   if( _disabled ) return session(*this);
   _stack.emplace_back( undo_state() );
   ++_active_sessions;
   return session(*this);
}
void undo_database::on_create( const object& obj )
{
   if( _disabled ) return;
   auto& state = _stack.back();
   auto index_id = object_id_type( obj.id.space(), obj.id.type(), 0 );
   auto itr = state.old_index_next_ids.find( index_id );
   if( itr == state.old_index_next_ids.end() )
      state.old_index_next_ids[index_id] = obj.id;
   state.new_ids.insert(obj.id);
}
void undo_database::on_modify( const object& obj )
{
   if( _disabled ) return;
   auto& state = _stack.back();
   auto itr =  state.old_values.find(obj.id);
   if( itr != state.old_values.end() ) return;
   state.old_values[obj.id] = obj.clone();
}
void undo_database::on_remove( const object& obj )
{
   if( _disabled ) return;
   auto& state = _stack.back();
   auto itr =  state.removed.find(obj.id);
   if( itr != state.removed.end() ) return;
   state.removed[obj.id] = obj.clone();
}

void undo_database::undo()
{ try {
   FC_ASSERT( !_disabled );
   FC_ASSERT( _active_sessions > 0 );
   disable();

   auto& state = _stack.back();
   for( auto& item : state.old_values )
   {
      _db.modify( _db.get_object( item.second->id ), [&]( object& obj ){ obj.move_from( *item.second ); } );
   }
   
   for( auto ritr = state.new_ids.rbegin(); ritr != state.new_ids.rend(); ++ritr  )
   {
      _db.remove( _db.get_object(*ritr) );
   }

   for( auto& item : state.old_index_next_ids )
   {
 //     wdump( (item) );
      _db.get_index( item.first.space(), item.first.type() ).set_next_id( item.second );
   }

   for( auto& item : state.removed )
      _db.insert( std::move(*item.second) );

   _stack.pop_back();
   enable();
   --_active_sessions;
} FC_CAPTURE_AND_RETHROW() }

void undo_database::merge()
{
   FC_ASSERT( _active_sessions > 0 );
   FC_ASSERT( _stack.size() >=2 );
   auto& state = _stack.back();
   auto& prev_state = _stack[_stack.size()-2];
   for( auto& obj : state.old_values )
      prev_state.old_values[obj.second->id] = std::move(obj.second);
   for( auto id : state.new_ids )
      prev_state.new_ids.insert(id);
   for( auto& item : state.old_index_next_ids )
   {
      if( prev_state.old_index_next_ids.find( item.first ) == prev_state.old_index_next_ids.end() )
         prev_state.old_index_next_ids[item.first] = item.second;
   }
   for( auto& obj : state.removed )
      prev_state.removed[obj.second->id] = std::move(obj.second);
   _stack.pop_back();
   --_active_sessions;
}
void undo_database::commit()
{
   --_active_sessions;
}

void undo_database::pop_commit()
{
   FC_ASSERT( _active_sessions == 0 );

   disable();
   try {
      auto& state = _stack.back();

      for( auto& item : state.old_values )
      {
  //       wdump( (item.second->id) );
         _db.modify( _db.get_object( item.second->id ), [&]( object& obj ){ obj.move_from( *item.second ); } );
      }
      
      for( auto ritr = state.new_ids.rbegin(); ritr != state.new_ids.rend(); ++ritr  )
      {
 //        wdump( (*ritr) );
         _db.remove( _db.get_object(*ritr) );
      }

      for( auto& item : state.old_index_next_ids )
      {
//         wdump( (item) );
         _db.get_index( item.first.space(), item.first.type() ).set_next_id( item.second );
      }

      for( auto& item : state.removed )
         _db.insert( std::move(*item.second) );
      
      _stack.pop_back();
   } 
   catch ( const fc::exception& e )
   {
      elog( "error poping commit ${e}", ("e", e.to_detail_string() )  );
      enable();
      throw;
   }
   enable();
}


} } // bts::chain
