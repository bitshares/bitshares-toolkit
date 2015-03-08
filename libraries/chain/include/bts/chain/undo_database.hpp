#pragma once
#include <bts/chain/object.hpp>

namespace bts { namespace chain {

   class database;

   /**
    * @class undo_database
    * @brief tracks changes to the state and allows changes to be undone
    *
    */
   class undo_database
   {
      public:
         undo_database( database& db ):_db(db){}

         class session
         {
            public:
               session( session&& mv )
               :_db(mv._db),_apply_undo(mv._apply_undo)
               {
                  mv._apply_undo = false;
               }
               ~session() { 
                  try {
                     if( _apply_undo ) _db.undo(); 
                  } 
                  catch ( const fc::exception& e )
                  {
                     if( std::uncaught_exception() )
                        throw;
                     else
                        elog( "${e}", ("e",e.to_detail_string() ) );
                  }
               }
               void commit() { _apply_undo = false; _db.commit();  }
               void undo()   { if( _apply_undo ) _db.undo(); _apply_undo = false; }
               void merge()  { if( _apply_undo ) _db.merge(); _apply_undo = false; }

               session& operator = ( session&& mv )
               {
                  if( this == &mv ) return *this;
                  if( _apply_undo ) _db.undo();
                  _apply_undo = mv._apply_undo;
                  mv._apply_undo = false;
                  return *this;
               }

            private:
               friend undo_database;
               session(undo_database& db): _db(db) {}
               undo_database& _db;
               bool _apply_undo = !_db._disabled;
         };

         void    disable();
         void    enable();

         session start_undo_session();
         /** this should be called just after obj is created */
         void on_create( const object& obj );
         /** this should be called just before obj is modified */
         void on_modify( const object& obj );
         /** this should be called just before an obj is removed */
         void on_remove( const object& obj );

         /**
          *  Removes the last committed session,
          *  note... this is dangerous if there are
          *  active sessions... thus active sessions should
          *  track 
          */
         void pop_commit();

      private:
         void undo();
         void merge();
         void commit();

         struct undo_state
         {
            unordered_map<object_id_type, unique_ptr<object> > old_values;
            unordered_map<object_id_type, object_id_type>      old_index_next_ids;
            set<object_id_type>                                new_ids;
            unordered_map<object_id_type, unique_ptr<object> > removed;
         };

         uint32_t               _active_sessions = 0;
         bool                   _disabled = true;
         std::deque<undo_state> _stack;
         database&              _db;
   };

} }
