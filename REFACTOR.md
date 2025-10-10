# List of things which really should be refactored:

 * If a function mallocs memory, it should return a pointer instead of using an output pointer
 * Should standardize how callbacks are handled, I think passing an struct with failling and success callbacks
 * Decide on a common error code framework, maybe matching that of libuv?
