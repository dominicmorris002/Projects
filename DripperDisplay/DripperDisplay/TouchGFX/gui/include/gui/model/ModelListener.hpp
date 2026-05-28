#ifndef MODEL_LISTENER_HPP
#define MODEL_LISTENER_HPP

#include "modbus_master.hpp"

class Model;

class ModelListener
{
public:
    ModelListener() : model(0) {}

    virtual ~ModelListener() {}

    void bind(Model *m) { model = m; }

    virtual void onPollResult(const MB_PollResult &result) {}

protected:
    Model *model;
};

#endif // MODEL_LISTENER_HPP
