#ifndef HHUOS_PORTEVENT_H
#define HHUOS_PORTEVENT_H

#include <kernel/events/Event.h>

class PortEvent : public Event {

public:

    PortEvent() = default;

    ~PortEvent() override = default;

    String getType() const override = 0;

    virtual char getChar() = 0;

};

#endif
