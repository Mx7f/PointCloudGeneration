/** \file App.cpp */
#include "App.h"

// Tells C++ to invoke command-line main() function even on OS X and Win32.
G3D_START_AT_MAIN();

int main(int argc, const char* argv[]) {
    {
        G3DSpecification g3dSpec;
        g3dSpec.audio = false;
        initGLG3D(g3dSpec);
    }

    GApp::Settings settings(argc, argv);

    // Change the window and other startup parameters by modifying the
    // settings class.  For example:
    settings.window.caption             = argv[0];
    // settings.window.debugContext     = true;

    // settings.window.width              =  854; settings.window.height       = 480;
    // settings.window.width            = 1024; settings.window.height       = 768;
     settings.window.width            = 1280; settings.window.height       = 720;
//    settings.window.width               = 1920; settings.window.height       = 1080;
    // settings.window.width            = OSWindow::primaryDisplayWindowSize().x; settings.window.height = OSWindow::primaryDisplayWindowSize().y;
    settings.window.fullScreen          = false;
    settings.window.resizable           = ! settings.window.fullScreen;
    settings.window.framed              = ! settings.window.fullScreen;
    settings.film.preferredColorFormats[0] = ImageFormat::RGBA32F();

    // Set to true for a significant performance boost if your app can't render at 60fps,
    // or if you *want* to render faster than the display.
    settings.window.asynchronous        = false;

    // No Guard Bands
    settings.depthGuardBandThickness    = Vector2int16(0, 0);
    settings.colorGuardBandThickness    = Vector2int16(0, 0);
    settings.dataDir                    = FileSystem::currentDirectory();
    settings.screenshotDirectory        = "../journal/";

    settings.renderer.deferredShading = true;
    settings.renderer.orderIndependentTransparency = false;


    return App(settings).run();
}


App::App(const GApp::Settings& settings) : GApp(settings) {
}

static void saveGBufferField(shared_ptr<GBuffer> gbuffer, GBuffer::Field field, const ImageFormat* format, const String& prefix) {
    auto ptb = gbuffer->texture(field)->toPixelTransferBuffer(format);
    Image::fromPixelTransferBuffer(ptb)->save(prefix + "_" + field.toString() + ".exr");
}

void App::saveGBuffer(const String& filename, int index, LightingEnvironment& environment) {
    shared_ptr<GBuffer> gbuffer = m_gbuffers[index];

    for (GBuffer::Field field : m_fieldsToSave) {
        saveGBufferField(gbuffer, field,      ImageFormat::RGB32F(), filename);
    }

    updateAO(index, environment);
    environment.ambientOcclusion = m_ambientOcclusion;
    RenderDevice* rd = RenderDevice::current;
    rd->pushState(m_framebuffer); {
        rd->setColorClearValue(Color3::black());
        rd->clear();
        renderDeferredShading(rd, gbuffer, environment);
    } rd->popState();
    Image::fromPixelTransferBuffer(m_framebuffer->texture(0)->toPixelTransferBuffer(ImageFormat::RGB32F()))->save(filename + "_color.exr");

    shared_ptr<Camera> cam = gbuffer->camera();
    Any a(Any::TABLE, "CameraParameters");
    a["frame"]                  = cam->frame();
    a["nearZ"]                  = cam->nearPlaneZ();
    a["farZ"]                   = cam->farPlaneZ();
    a["fieldOfViewAngle"]       = cam->fieldOfViewAngle();
    a["fieldOfViewDirection"]   = cam->fieldOfViewDirection();
    a.save(filename + ".CameraParameters.Any");
}

// Called before the application loop begins.  Load data here and
// not in the constructor so that common exceptions will be
// automatically caught.
void App::onInit() {
    GApp::onInit();
    setFrameDuration(1.0f / 60.0f);
    m_numLayers = 2;
    m_depthPeelSeparationHint = 0.1f;

    //static auto test = Texture::fromFile(System::findDataFile("gbuffer0_WS_POSITION.exr"));

    m_savingGBuffers = false;

    // Add Fields you want to save here
    //    m_fieldsToSave.append(GBuffer::Field::WS_POSITION);
    m_fieldsToSave.append(GBuffer::Field::CS_POSITION);
    m_fieldsToSave.append(GBuffer::Field::CS_FACE_NORMAL);


    for (GBuffer::Field field : m_fieldsToSave) {
      // Make the formats be floating point
      m_gbufferSpecification.encoding[field] = ImageFormat::RGB32F();
    }
    
    for (int i = 0; i < m_numLayers; ++i) {
        m_gbuffers.append(GBuffer::create(m_gbufferSpecification, format("GBuffer Layer %d", i)));
    }

    // Call setScene(shared_ptr<Scene>()) or setScene(MyScene::create()) to replace
    // the default scene here.
    
    showRenderingStats      = true;

    makeGUI();
    // For higher-quality screenshots:
    // developerWindow->videoRecordDialog->setScreenShotFormat("PNG");
    // developerWindow->videoRecordDialog->setCaptureGui(false);
    developerWindow->cameraControlWindow->moveTo(Point2(developerWindow->cameraControlWindow->rect().x0(), 0));
    loadScene(
//        "Hair"
         //"G3D Sponza"
        "G3D Cornell Box" // Load something simple
        //developerWindow->sceneEditorWindow->selectedSceneName()  // Load the first scene encountered 
        );
}


void App::makeGUI() {
    // Initialize the developer HUD (using the existing scene)
    createDeveloperHUD();
    debugWindow->setVisible(true);
    developerWindow->videoRecordDialog->setEnabled(true);

    GuiPane* infoPane = debugPane->addPane("Info", GuiTheme::ORNATE_PANE_STYLE);
    // TODO: Allow Num Layers to change in the middle of the program
    infoPane->addNumberBox("Num Layers", &m_numLayers)->setEnabled(false);
    GuiNumberBox<float>* b = infoPane->addNumberBox("Depth Peel Separation", &m_depthPeelSeparationHint);
    b->setCaptionWidth(130);
    infoPane->addButton("Save GBuffers", this, &App::setSaveGBuffers);
    infoPane->pack();

    debugWindow->pack();
    debugWindow->setRect(Rect2D::xywh(0, 0, (float)window()->width(), debugWindow->rect().height()));
}

static void setPositionsToNaNs(RenderDevice* rd, shared_ptr<Texture> positionTexture) {
  static shared_ptr<Framebuffer> nanFB = Framebuffer::create("NaN Framebuffer");
  nanFB->set(Framebuffer::COLOR0, positionTexture);
  rd->pushState(nanFB); {
    // Can't set clear color to nan in G3D do to a != check...
    glClearColor(fnan(), fnan(), fnan(), 0.0);
    rd->clear();
    rd->setColorClearValue(Color3(5, 0, 0));
  } rd->popState();
 
}

void App::computeGBuffers(RenderDevice* rd, Array<shared_ptr<Surface> >& all) {
    BEGIN_PROFILER_EVENT("App::computeGBuffers");

    for (int i = 0; i < m_gbuffers.size(); ++i) {
        m_gbuffers[i]->prepare(rd, activeCamera(), 0, -(float)previousSimTimeStep(), m_settings.depthGuardBandThickness, m_settings.colorGuardBandThickness);
    }

    // Hack to set all non convered pixels to nan in the position buffer; TODO: make more efficient/elegant

    for (int i = 0; i < m_gbuffers.size(); ++i) {
      if (m_fieldsToSave.contains(GBuffer::Field::WS_POSITION)) {
	setPositionsToNaNs(rd, m_gbuffers[i]->texture(GBuffer::Field::WS_POSITION));
      }
      if (m_fieldsToSave.contains(GBuffer::Field::CS_POSITION)) {
	setPositionsToNaNs(rd, m_gbuffers[i]->texture(GBuffer::Field::CS_POSITION));
      }
      
    }


    Array<shared_ptr<Surface> > sortedVisible;
    Surface::cull(activeCamera()->frame(), activeCamera()->projection(), rd->viewport(), all, sortedVisible);
    Surface::sortFrontToBack(sortedVisible, activeCamera()->frame().lookVector());
    // TODO: Holdover from DeepGBufferRadiosity... why?
    glDisable(GL_DEPTH_CLAMP);
    Surface::renderIntoGBuffer(rd, sortedVisible, m_gbuffers[0], activeCamera()->previousFrame(), activeCamera()->expressivePreviousFrame());
    for (int i = 1; i < m_gbuffers.size(); ++i) {
        Surface::renderIntoGBuffer(rd, sortedVisible, m_gbuffers[i], activeCamera()->previousFrame(), activeCamera()->expressivePreviousFrame(), m_gbuffers[i-1]->texture(GBuffer::Field::DEPTH_AND_STENCIL), m_depthPeelSeparationHint);
    }
    END_PROFILER_EVENT();
}

void App::updateAO(int layer, LightingEnvironment& environment) {
    AmbientOcclusionSettings settings = environment.ambientOcclusionSettings;
    shared_ptr<Texture> depthPeelTexture = shared_ptr<Texture>();
    if (layer == (m_numLayers - 1)) {
        settings.useDepthPeelBuffer = false;
    } else {
        depthPeelTexture = m_gbuffers[layer + 1]->texture(GBuffer::Field::DEPTH_AND_STENCIL);
    }

    m_ambientOcclusion->update(RenderDevice::current,
        settings,
        activeCamera(), 
        m_gbuffers[layer]->texture(GBuffer::Field::DEPTH_AND_STENCIL),
        depthPeelTexture,
        m_gbuffers[layer]->texture(GBuffer::Field::CS_NORMAL),
        m_gbuffers[layer]->texture(GBuffer::Field::SS_POSITION_CHANGE),
        m_settings.depthGuardBandThickness - m_settings.colorGuardBandThickness);
}

void App::computeShadows(RenderDevice* rd, Array<shared_ptr<Surface> >& all, LightingEnvironment& environment) {
    BEGIN_PROFILER_EVENT("App::computeShadows");
    environment = scene()->lightingEnvironment();
    updateAO(0, environment);
    
    environment.ambientOcclusion = m_ambientOcclusion;

    static RealTime lastLightingChangeTime = 0;
    RealTime lightingChangeTime = max(scene()->lastEditingTime(), max(scene()->lastLightChangeTime(), scene()->lastVisibleChangeTime()));
    if (lightingChangeTime > lastLightingChangeTime) {
        lastLightingChangeTime = lightingChangeTime;
        Surface::renderShadowMaps(rd, environment.lightArray, all);
    }
    END_PROFILER_EVENT();
}

void App::renderDeferredShading(RenderDevice* rd, const shared_ptr<GBuffer>& gbuffer, const LightingEnvironment& environment) {
    // Make a pass over the screen, performing shading
    rd->push2D(); {
        rd->setGuardBandClip2D(gbuffer->colorGuardBandThickness());


        Args args;

        environment.setShaderArgs(args);
        gbuffer->setShaderArgsRead(args, "gbuffer_");

        args.setRect(rd->viewport());

        LAUNCH_SHADER("DefaultRenderer_deferredShade.pix", args);
    } rd->pop2D();
}

void App::saveAllGBuffers(LightingEnvironment& environment) {
    for (int i = 0; i < m_gbuffers.size(); ++i) {
        saveGBuffer(format("gbuffer%d", i), i, environment);
    }
}

void App::onGraphics3D(RenderDevice* rd, Array<shared_ptr<Surface> >& all) {
    if (!scene()) {
        if ((submitToDisplayMode() == SubmitToDisplayMode::MAXIMIZE_THROUGHPUT) && (!rd->swapBuffersAutomatically())) {
            swapBuffers();
        }
        rd->clear();
        rd->pushState(); {
            rd->setProjectionAndCameraMatrix(activeCamera()->projection(), activeCamera()->frame());
            drawDebugShapes();
        } rd->popState();
        return;
    }

    for (int i = 0; i < m_gbuffers.size(); ++i) {
        m_gbuffers[i]->setSpecification(m_gbufferSpecification);
        m_gbuffers[i]->resize(m_framebuffer->width(), m_framebuffer->height());
        m_gbuffers[i]->prepare(rd, activeCamera(), 0, -(float)previousSimTimeStep(), m_settings.depthGuardBandThickness, m_settings.colorGuardBandThickness);
    }
    

    // Bind the main framebuffer
    rd->pushState(m_framebuffer); {
        rd->setProjectionAndCameraMatrix(activeCamera()->projection(), activeCamera()->frame());
        rd->clear();

        LightingEnvironment environment;

        // Remove Skybox
        for (int i = 0; i < all.size(); ++i) {
            if (notNull(dynamic_pointer_cast<SkyboxSurface>(all[i]))) {
                all.fastRemove(i);
                --i;
            }
        }

        computeGBuffers(rd, all);
        computeShadows(rd, all, environment);

        // Remove everything that was in the G-buffer, except for the skybox, which is emissive
        // and benefits from a forward pass
        for (int i = 0; i < all.size(); ++i) {
            if (all[i]->canBeFullyRepresentedInGBuffer(m_gbuffer->specification())) {
                all.fastRemove(i);
                --i;
            }
        }

        if (m_savingGBuffers) {
            saveAllGBuffers(environment);
            m_savingGBuffers = false;
        }

        renderDeferredShading(rd, m_gbuffers[0], environment);

        /* TODO: Re-enable DoF, Motion Blur
        m_depthOfField->apply(rd, m_framebuffer->texture(0), m_gbuffer->texture(GBuffer::Field::DEPTH_AND_STENCIL), activeCamera(), m_settings.depthGuardBandThickness);

        m_motionBlur->apply(rd, m_framebuffer->texture(0), m_gbuffer->texture(GBuffer::Field::SS_EXPRESSIVE_MOTION),
            m_gbuffer->texture(GBuffer::Field::DEPTH_AND_STENCIL), activeCamera(),
            m_settings.depthGuardBandThickness);
            */

    } rd->popState();

    // We're about to render to the actual back buffer, so swap the buffers now.
    // This call also allows the screenshot and video recording to capture the
    // previous frame just before it is displayed.
    if (submitToDisplayMode() == SubmitToDisplayMode::MAXIMIZE_THROUGHPUT) {
        swapBuffers();
    }

    // Clear the entire screen (needed even though we'll render over it, since
    // AFR uses clear() to detect that the buffer is not re-used.)
    rd->clear();

    // Perform gamma correction, bloom, and SSAA, and write to the native window frame buffer
    m_film->exposeAndRender(rd, activeCamera()->filmSettings(), m_framebuffer->texture(0));
}


void App::onAI() {
    GApp::onAI();
    // Add non-simulation game logic and AI code here
}


void App::onNetwork() {
    GApp::onNetwork();
    // Poll net messages here
}


void App::onSimulation(RealTime rdt, SimTime sdt, SimTime idt) {
    GApp::onSimulation(rdt, sdt, idt);

    // Example GUI dynamic layout code.  Resize the debugWindow to fill
    // the screen horizontally.
    debugWindow->setRect(Rect2D::xywh(0, 0, (float)window()->width(), debugWindow->rect().height()));
}


bool App::onEvent(const GEvent& event) {
    // Handle super-class events
    if (GApp::onEvent(event)) { return true; }

    // If you need to track individual UI events, manage them here.
    // Return true if you want to prevent other parts of the system
    // from observing this specific event.
    //
    // For example,
    // if ((event.type == GEventType::GUI_ACTION) && (event.gui.control == m_button)) { ... return true; }
    // if ((event.type == GEventType::KEY_DOWN) && (event.key.keysym.sym == GKey::TAB)) { ... return true; }

    return false;
}


void App::onUserInput(UserInput* ui) {
    GApp::onUserInput(ui);
    (void)ui;
    // Add key handling here based on the keys currently held or
    // ones that changed in the last frame.
}


void App::onPose(Array<shared_ptr<Surface> >& surface, Array<shared_ptr<Surface2D> >& surface2D) {
    GApp::onPose(surface, surface2D);

    // Append any models to the arrays that you want to later be rendered by onGraphics()
}


void App::onGraphics2D(RenderDevice* rd, Array<shared_ptr<Surface2D> >& posed2D) {
    // Render 2D objects like Widgets.  These do not receive tone mapping or gamma correction.
    Surface2D::sortAndRender(rd, posed2D);
}


void App::onCleanup() {
    // Called after the application loop ends.  Place a majority of cleanup code
    // here instead of in the constructor so that exceptions can be caught.
}


void App::endProgram() {
    m_endProgram = true;
}
