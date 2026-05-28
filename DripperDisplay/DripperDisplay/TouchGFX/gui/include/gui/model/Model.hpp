// =============================================================
// Model.hpp  —  DripperDisplay TouchGFX Model
// Add tickCounter here so Model.cpp compiles cleanly.
// Merge this into your existing Model.hpp — only the two
// lines marked ← ADD are new.
// =============================================================
#ifndef MODEL_HPP
#define MODEL_HPP

class ModelListener;

class Model
{
public:
    Model();

    void bind(ModelListener *listener) { modelListener = listener; }
    void tick();

protected:
    ModelListener *modelListener;
    int tickCounter;    // ← ADD this line to your existing Model.hpp
};

#endif // MODEL_HPP