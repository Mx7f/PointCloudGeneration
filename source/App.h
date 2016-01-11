/**
  \file App.h

  The G3D 10.00 default starter app is configured for OpenGL 3.3 and
  relatively recent GPUs.
 */
#ifndef App_h
#define App_h
#include <G3D/G3DAll.h>

/** Application framework. */
class App : public GApp {
protected:

  Array<GBuffer::Field> m_fieldsToSave;
  
    /** True when we need to save GBuffers this frame */
    bool m_savingGBuffers;

    int m_numLayers;

    float m_depthPeelSeparationHint;

    Array<shared_ptr<GBuffer>> m_gbuffers;

    void saveAllGBuffers(LightingEnvironment& environment);

    /** Called from onInit */
    void makeGUI();

    void saveGBuffer(const String& filename, int index, LightingEnvironment& environment);

    void updateAO(int layer, LightingEnvironment& environment);

    void setSaveGBuffers() {
        m_savingGBuffers = true;
    }

    void renderDeferredShading(RenderDevice* rd, const shared_ptr<GBuffer>& gbuffer, const LightingEnvironment& environment);

    void computeGBuffers(RenderDevice* rd, Array<shared_ptr<Surface> >& all);

    void computeShadows(RenderDevice* rd, Array<shared_ptr<Surface> >& all, LightingEnvironment& environment);

public:
    
    

    App(const GApp::Settings& settings = GApp::Settings());

    virtual void onInit() override;
    virtual void onAI() override;
    virtual void onNetwork() override;
    virtual void onSimulation(RealTime rdt, SimTime sdt, SimTime idt) override;
    virtual void onPose(Array<shared_ptr<Surface> >& posed3D, Array<shared_ptr<Surface2D> >& posed2D) override;

    // You can override onGraphics if you want more control over the rendering loop.
    // virtual void onGraphics(RenderDevice* rd, Array<shared_ptr<Surface> >& surface, Array<shared_ptr<Surface2D> >& surface2D) override;

    virtual void onGraphics3D(RenderDevice* rd, Array<shared_ptr<Surface> >& surface3D) override;
    virtual void onGraphics2D(RenderDevice* rd, Array<shared_ptr<Surface2D> >& surface2D) override;

    virtual bool onEvent(const GEvent& e) override;
    virtual void onUserInput(UserInput* ui) override;
    virtual void onCleanup() override;
    
    /** Sets m_endProgram to true. */
    virtual void endProgram();
};

#endif
