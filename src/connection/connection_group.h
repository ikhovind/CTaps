#ifndef CT_CONNECTION_GROUP_H
#define CT_CONNECTION_GROUP_H
#include "ctaps.h"

/**
  * @brief Add a connection to a connection group.
  *
  * @param[in,out] group Connection group to add to
  * @param[in] connection Connection to add
  *
  * @return 0 on success, -EINVAL if connection or group is NULL, -EEXIST if connection already in group
  */
int ct_connection_group_add_connection(ct_connection_group_t* group, ct_connection_t* connection);


#endif // CT_CONNECTION_GROUP_H
