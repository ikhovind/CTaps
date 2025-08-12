//
// Created by ikhovind on 12.08.25.
//

#ifndef SELECTION_PROPERTIES_H
#define SELECTION_PROPERTIES_H

enum SelectionPreference {
    kRequire = 0,
    kPrefer,
    kNoPreference,
    kAvoid,
    kProhibit
};

// TODO - specific struct or just "name" field in generic struct for a selection property?

struct SelectionProperties {
    enum SelectionPreference preference;
};

#endif //SELECTION_PROPERTIES_H
