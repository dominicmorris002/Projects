/* New file to work on
Intent is this would store an enum of all error numbers (or maybe that needs to be in cloud.cpp with the rest)
Set up a message queue that other files can include and throw messages into
cloud.cpp will unload message queue and send to the cloud
consider using a periodic unloading (e.g. 30s) so groups of errors are transmitted in a single block

*/