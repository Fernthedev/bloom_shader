#include "main.hpp"
#include "opengl/Shader.hpp"
#include "opengl/Shaders.hpp"

#include "coro.hpp"

#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"
#include "UnityEngine/SceneManagement/Scene.hpp"
#include "UnityEngine/SceneManagement/LoadSceneMode.hpp"
#include "UnityEngine/Events/UnityAction_2.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/Resources.hpp"

#include "GlobalNamespace/MainSettingsModelSO.hpp"
#include "GlobalNamespace/MainSystemInit.hpp"
#include "GlobalNamespace/BoolSO.hpp"
#include "GlobalNamespace/IntSO.hpp"
#include "GlobalNamespace/BloomPrePassBloomTextureEffectSO.hpp"
#include "GlobalNamespace/BloomPrePassEffectContainerSO.hpp"

#include "custom-types/shared/coroutine.hpp"
#include "custom-types/shared/register.hpp"


#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>

static ModInfo modInfo; // Stores the ID and version of our mod, and is sent to the modloader upon startup

// Loads the config from disk using our modInfo, then returns it for use
Configuration& getConfig() {
    static Configuration config(modInfo);
    config.Load();
    return config;
}

// Returns a logger, useful for printing debug messages
Logger& getLogger() {
    static Logger* logger = new Logger(modInfo);
    return *logger;
}

struct Task {
    int height;
    int width;
    int depth;
};


static std::unordered_map<int,std::shared_ptr<Task>> tasks;
static std::shared_mutex tasks_mutex;
static int next_event_id = 1;

extern "C" int makeRequest_mainThread() {
    // Create the task
    auto task = std::make_shared<Task>();
    int event_id = next_event_id;
    next_event_id++;


    // Save it (lock because possible vector resize)
    std::unique_lock lock(tasks_mutex);
    tasks[event_id] = task;
    lock.unlock();


    return event_id;
}

extern "C" void dispose(int event_id) {
    // Remove from tasks
    std::unique_lock lock(tasks_mutex);
    std::shared_ptr<Task> task = tasks[event_id];
    // free(task->data);
    tasks.erase(event_id);
    lock.unlock();
}

// renderQuad() renders a 1x1 XY quad in NDC
// -----------------------------------------
unsigned int quadVAO = 0;
unsigned int quadVBO;
void renderQuad()
{
    if (quadVAO == 0)
    {
        float quadVertices[] = {
                // positions        // texture Coords
                -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
                -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
                1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
                1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
        // setup plane VAO
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}


// Code from xyonico, thank you very much!
void BlitShader(GLuint cameraSrcTexture, Shader& shader)
{
    // This function assumes that a framebuffer has already been bound with a texture different from cameraSrcTexture.


    // Prepare the shader
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cameraSrcTexture);
    shader.use();


    // Prepare the VBO
    GLuint quadVertices;
    glGenVertexArrays(1, &quadVertices);


    glBindVertexArray(quadVertices);


    // For some reason this is needed. Likely as an optimization
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);


    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);


    glDeleteVertexArrays(1, &quadVertices);
}

static Shader shaderBloom;
static Shader shaderBlur;
static Shader shaderBloomFinal;

unsigned int pingpongFBO[2];
unsigned int pingpongColorbuffers[2];
unsigned int colorBuffers[2];

GLint drawFboId = 0, readFboId = 0;

extern "C" void bloomshader_Initialize(int eventId) {
    auto task = tasks[eventId];

    auto const SCR_WIDTH = task->width;
    auto const SCR_HEIGHT = task->height;

    try {
        static auto lazyInitialize = []() {
            shaderBloom = Shaders::bloom();
            shaderBlur = Shaders::gaussian();
            shaderBloomFinal = Shaders::final_process();
            return 0;
        }();
    } catch (...) {
        SAFE_ABORT_MSG("Shader error!");
        throw;
    }

    // EXPENSIVE
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFboId);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFboId);


    // create 2 floating point color buffers (1 for normal rendering, other for brightness threshold values)
    glGenTextures(2, colorBuffers);
    for (unsigned int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D, colorBuffers[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);  // we clamp to the edge as the blur filter would otherwise sample repeated texture values!
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // attach texture to framebuffer
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, colorBuffers[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ping-pong-framebuffer for blurring
    glGenFramebuffers(2, pingpongFBO);
    glGenTextures(2, pingpongColorbuffers);
    for (unsigned int i = 0; i < 2; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[i]);
        glBindTexture(GL_TEXTURE_2D, pingpongColorbuffers[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // we clamp to the edge as the blur filter would otherwise sample repeated texture values!
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pingpongColorbuffers[i], 0);
        // also check if framebuffers are complete (no need for depth buffer)
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            PLogger.fmtLog<Paper::LogLevel::INF>("Framebuffer not complete!");
    }

    // shader configuration
    // --------------------
    shaderBlur.use();
    shaderBlur.setInt("image", 0);
    shaderBloomFinal.use();
    shaderBloomFinal.setInt("scene", 0);
    shaderBloomFinal.setInt("bloomBlur", 1);

    dispose(eventId);
}

// https://learnopengl.com/Advanced-Lighting/Bloom
void bloomshader_Apply(int eventId) {
//    auto task = tasks[eventId];
//
//    auto const SCR_WIDTH = task->width;
//    auto const SCR_HEIGHT = task->height;



//    // BLOOM
//    unsigned int hdrFBO;
//    glGenFramebuffers(1, &hdrFBO);
//    glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
//    // create 2 floating point color buffers (1 for normal rendering, other for brightness threshold values)
//    unsigned int colorBuffers[2];
//    glGenTextures(2, colorBuffers);
//    for (unsigned int i = 0; i < 2; i++)
//    {
//        glBindTexture(GL_TEXTURE_2D, colorBuffers[i]);
//        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
//        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);  // we clamp to the edge as the blur filter would otherwise sample repeated texture values!
//        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//        // attach texture to framebuffer
//        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, colorBuffers[i], 0);
//    }
//    // create and attach depth buffer (renderbuffer)
//    unsigned int rboDepth;
//    glGenRenderbuffers(1, &rboDepth);
//    glBindRenderbuffer(GL_RENDERBUFFER, rboDepth);
//    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, SCR_WIDTH, SCR_HEIGHT);
//    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rboDepth);
//    // tell OpenGL which color attachments we'll use (of this framebuffer) for rendering
//    unsigned int attachments[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
//    glDrawBuffers(2, attachments);
//    // finally check if framebuffer is complete
//    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
//        std::cout << "Framebuffer not complete!" << std::endl;
//    glBindFramebuffer(GL_FRAMEBUFFER, 0);


    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // 2. blur bright fragments with two-pass Gaussian Blur
    // --------------------------------------------------
    bool horizontal = true, first_iteration = true;
    unsigned int amount = 10;
    shaderBlur.use();
    for (unsigned int i = 0; i < amount; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[horizontal]);
        shaderBlur.setInt("horizontal", horizontal);
        glBindTexture(GL_TEXTURE_2D, first_iteration ? colorBuffers[1] : pingpongColorbuffers[!horizontal]);  // bind texture of other framebuffer (or scene if first iteration)
        renderQuad();
        horizontal = !horizontal;
        if (first_iteration)
            first_iteration = false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 3. now render floating point color buffer to 2D quad and tonemap HDR colors to default framebuffer's (clamped) color range
    // --------------------------------------------------------------------------------------------------------------------------
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    shaderBloomFinal.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorBuffers[0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, pingpongColorbuffers[!horizontal]);
    shaderBloomFinal.setFloat("exposure", 1.0f);
    renderQuad();
}

using GLIssuePluginEvent = function_ptr_t<void, void*, int>;
GLIssuePluginEvent GetGLIssuePluginEvent() {
    static GLIssuePluginEvent issuePluginEvent = il2cpp_utils::resolve_icall<void, void*, int>("UnityEngine.GL::GLIssuePluginEvent");
    return issuePluginEvent;
}


// Called at the early stages of game loading
extern "C" void setup(ModInfo& info) {
    info.id = MOD_ID;
    info.version = VERSION;
    modInfo = info;
	
    getConfig().Load(); // Load the config file
    getLogger().info("Completed setup!");
}

// https://github.com/ItsKaitlyn03/AnyTweaks/blob/e4509aab0d6a6201d8c3b0ee13ca077c22fbfb53/src/hooks/MainSettingsModelSO.cpp#L10-L25
MAKE_HOOK_MATCH(
        MainSettingsModelSO_Load,
        &GlobalNamespace::MainSettingsModelSO::Load,
        void,
        GlobalNamespace::MainSettingsModelSO* self,
        bool forced
) {
    MainSettingsModelSO_Load(self, forced);


    self->dyn_mainEffectGraphicsSettings()->set_value(true);
    self->dyn_bloomPrePassGraphicsSettings()->set_value(true);
}


MAKE_HOOK_MATCH(
        MainSystemInit_Init,
        &GlobalNamespace::MainSystemInit::Init,
        void,
        GlobalNamespace::MainSystemInit* self
) {
    using namespace GlobalNamespace;
    using namespace UnityEngine;

    MainSystemInit_Init(self);


    auto bloomPrePassEffects = Resources::FindObjectsOfTypeAll<BloomPrePassBloomTextureEffectSO*>();
    for (int i = 0; i < bloomPrePassEffects.Length(); i++) {
        BloomPrePassBloomTextureEffectSO* bloomPrePassEffect = bloomPrePassEffects[i];
        if (bloomPrePassEffect->get_name() == "BloomPrePassLDBloomTextureEffect") {
            self->dyn__bloomPrePassEffectContainer()->Init(bloomPrePassEffect);
        }
    }

}

custom_types::Helpers::Coroutine renderCoroutine() {
    auto eventId = makeRequest_mainThread();
    GetGLIssuePluginEvent()(reinterpret_cast<void*>(bloomshader_Initialize), eventId);

    while (true) {
        GetGLIssuePluginEvent()(reinterpret_cast<void*>(bloomshader_Apply), -1);
        co_yield nullptr;
    }
}

DEFINE_TYPE(BloomShaderGLSL, BloomShaderCoro);

// Called later on in the game loading - a good time to install function hooks
extern "C" void load() {
    il2cpp_functions::Init();

    if (!Paper::Logger::IsInited()) {
        Paper::Logger::Init("/sdcard/Android/data/com.beatgames.beatsaber/files/logs/paper");
    }

    Paper::Logger::RegisterFileContextId(PLogger.tag);

    Modloader::requireMod("anytweaks");

    getLogger().info("Installing hooks...");
    custom_types::Register::AutoRegister();
    INSTALL_HOOK(getLogger(), MainSettingsModelSO_Load);
    INSTALL_HOOK(getLogger(), MainSystemInit_Init);
    // Install our hooks (none defined yet)
    getLogger().info("Installed all hooks!");

    std::function<void(UnityEngine::SceneManagement::Scene, ::UnityEngine::SceneManagement::LoadSceneMode)> onSceneChanged = [](UnityEngine::SceneManagement::Scene scene, ::UnityEngine::SceneManagement::LoadSceneMode) {
        if (!scene.IsValid()) return;

        PAPER_IL2CPP_CATCH_HANDLER(
            auto go = UnityEngine::GameObject::New_ctor("BloomShaderGLSL");
            UnityEngine::Object::DontDestroyOnLoad(go);
            go->AddComponent<BloomShaderGLSL::BloomShaderCoro*>()->StartCoroutine(
                    custom_types::Helpers::CoroutineHelper::New(renderCoroutine()));
        )
    };

    auto delegate = il2cpp_utils::MakeDelegate
            <UnityEngine::Events::UnityAction_2<::UnityEngine::SceneManagement::Scene, ::UnityEngine::SceneManagement::LoadSceneMode>*>(onSceneChanged);

    UnityEngine::SceneManagement::SceneManager::add_sceneLoaded(delegate);
}