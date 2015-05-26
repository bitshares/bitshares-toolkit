Intro for new developers
------------------------

This is a quick introduction to get new developers up to speed on Graphene.

Starting Graphene
-----------------

    git clone https://gitlab.bitshares.org/dlarimer/graphene
    cd graphene
    git submodule update --init --recursive

TODO:  Are recursive flags needed for submodules?

    cmake -DCMAKE_BUILD_TYPE=Debug .
    make
    cd programs
    ./witness_node

In a separate window, start `cli_wallet`:

    ./cli_wallet

You will get a transport error because the default initialization of witness node configuration does not specify a port.
You can edit `witneess_node_data_dir/config.json` like this:

    "websocket_endpoint": "127.0.0.1:8090"

If you send private keys over this connection, clearly `websocket_endpoint` should be bound to localhost for security.

Code coverage testing
---------------------

TODO:  Write something here

Unit testing
------------

TODO:  Write something here

Core mechanics
--------------

- Witnesses
- Key members
- Price feeds
- Global parameters
- Voting on witnesses
- Voting on key members
- Witness pay
- Transfers
- Markets
- Escrow
- Recurring payments

Gotchas
-------

- Key objects can actually contain a key or address

Witness node
------------

The role of the witness node is to broadcast transactions, download blocks, and optionally sign them.

TODO:  How do you get block signing keys into the witness node?

How to use fc async to do recurring tasks
-----------------------------------------

    _my_task = fc::async( callable, "My Task" );
    _my_task = fc::schedule( callable, "My Task 2", exec_time );

Stuff to know about the code
----------------------------

`static_variant<t1, t2>` is a *union type* which says "this variable may be either t1 or t2."  It is serializable if t1 and t2 are both serializable (TODO:  Is this accurate?)

The 'operations.hpp` documents the available operations, and `database_fixture.hpp` shows the way to do many things.

Tests also show the way to do many things, but are often cluttered with code that generates corner cases to try to break things in every possible way.

Visitors are at the end of `operations.hpp` after the large typedef for `operation` as a `static_variant`.  TODO:  They should be refactored into a separate header.

When using `modify()`, you can pass in either an `object_id` or an `object`.  Passing in an `object` is faster as it avoids a lookup.

Downcasting stuff
-----------------

- You have an `object_id_type` and want to downcast it to a `key_id_type` : `key_id_type( object_id )`
- You have an `operation_result` and want to downcast it to an `object_id_type` : `op_result.get<object_id_type>()`
- Since `operation_result` is a `static_variant`, the above is also how you downcast `static_variant`

Running specific tests
----------------------

- `tests/chain_tests -t block_tests/name_of_test`

Debugging FC exceptions with GDB
--------------------------------

- `catch throw`

TODO: Questions
---------------

This section contains questions for more experienced developers to answer.

Is there a way to generate help with parameter names and method descriptions?

Is there a way to allow external program to drive `cli_wallet` via websocket, JSONRPC, or HTTP?