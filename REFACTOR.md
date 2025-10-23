# List of things which really should be refactored:

 * If a function mallocs memory, it should return a pointer instead of using an output pointer
 * ~~Should standardize how callbacks are handled, I think passing an struct with failling and success callbacks~~
    * The protocol interface should not take callback struct as parameter, preconnection_initiate
      takes this and places them in the Connection
 * ~~Decide on a common error code framework, maybe matching that of libuv?~~
 * Simplify the socket_manager.c functionality
   * Find some way of making the socket manager more unified with connection-oriented vs connectionless protocols
   * Multiplexing etc. does not make sense for TCP for example.
