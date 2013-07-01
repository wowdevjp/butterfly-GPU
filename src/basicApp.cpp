#include "cinder/app/AppBasic.h"
#include "cinder/Rand.h"
#include "cinder/MayaCamUI.h"
#include "cinder/Matrix.h"
#include "cinder/params/Params.h"
#include "cinder/gl/GlslProg.h"
#include "cinder/gl/Vbo.h"
#include "cinder/ImageIo.h"
#include "cinder/gl/Texture.h"
#include "cinder/Ray.h"
#include "cinder/qtime/MovieWriter.h"

#import <OpenCL/opencl.h>
#import <OpenGL/OpenGL.h>

using namespace ci;
using namespace ci::app;

#include <list>
using namespace std;

#include <vector>
#include <boost/shared_ptr.hpp>
#include <boost/utility.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>

#include "butterfly.cl.h"
#include "Dialog.h"

//private functions
namespace
{
    static float remap(float value, float inputMin, float inputMax, float outputMin, float outputMax)
    {
        return (value - inputMin) * ((outputMax - outputMin) / (inputMax - inputMin)) + outputMin;
    }
    
    typedef struct
    {
        Vec3f position0;
        Vec3f position1;
        Vec2f texcoord;
    }ButterflyClassVertex;

    namespace cl
    {
        cl_float3 toCLVec3f(Vec3f v)
        {
            cl_float3 vcl = {v.x, v.y, v.z};
            return vcl;
        }
    }
    
    enum
    {
        SHADER_SINGLE_BUTTERFLY = 0,
        SHADER_MULTIPLE_BUTTERFLY,
    };
}


class BasicApp : public AppBasic {
 public:
    void prepareSettings( Settings *settings );
    void setup();
    
    void mouseMove(MouseEvent event);
	void mouseDrag(MouseEvent event );
    void mouseDown(MouseEvent event);
	void keyDown( KeyEvent event );
    
    void resize();
    
	void draw();
private:
    void setupButterfly();
    void tearduwnButterfly();
private:
    MayaCamUI m_camera;
    double m_lastTime;

    //panel
    params::InterfaceGl m_interface;
    struct{
        bool isPause;
        bool isAdditiveBlending;
        bool isManualMove;
        bool isUseCpu;
        float size;
        
        int numberOfButterfly;
        
        int shaderType;
        int butterflyType;
        
        Color backgroundColor;
    }settings;
    
    cinder::gl::GlslProg m_shader1;
    cinder::gl::GlslProg m_shader2;
    
    double m_time;
    
    gl::Vbo m_butterfly_class_vbo;
    int m_number_of_vertices_class;
    
    gl::Vbo m_butterfly_instance_vbo;
    int m_number_of_instance;
    
    std::vector<cinder::gl::Texture> m_images;
    
    //cl
    dispatch_queue_t m_clqueue;
    dispatch_queue_t m_cpuqueue;
    dispatch_group_t m_group;
    
    ButterflyInstance *m_butterfly_instance_gpu;
    cl_float3 *m_buffetfly_velocity_gpu;
    
    float m_fps;
    
    Vec2i m_mousePos;
    
    qtime::MovieWriter	mMovieWriter;
};

void BasicApp::prepareSettings( Settings *settings ) {
//    settings->setWindowSize( 1920, 1080 );
    settings->setWindowSize( 1080, 768 );
    settings->setFrameRate( 60.0f );
    
}
void BasicApp::setup()
{
    CameraPersp cam;
	cam.setEyePoint( Vec3f(200.0f, 200.0f, 200.0f) );
	cam.setCenterOfInterestPoint( Vec3f(0.0f, 0.0f, 0.0f) );
	cam.setPerspective( 60.0f, getWindowAspectRatio(), 1.0f, 1000.0f );
	m_camera.setCurrentCam( cam );
 
    m_fps = 60.0;
    settings.isPause = false;
    settings.isAdditiveBlending = true;
    settings.isManualMove = false;
    settings.isUseCpu = false;
    settings.size = 1.0f;
    settings.shaderType = SHADER_MULTIPLE_BUTTERFLY;
    settings.butterflyType = 0;
    
    settings.numberOfButterfly = 100000; /*default value*/
    m_interface = params::InterfaceGl("settings", Vec2i(300, 400));
    m_interface.addParam("fps", &m_fps);
    m_interface.addParam("additive blending", &settings.isAdditiveBlending, "");
    m_interface.addParam("pause", &settings.isPause);
    m_interface.addParam("manual move", &settings.isManualMove);
    m_interface.addParam("size", &settings.size, "step=0.1");
    m_interface.addParam("use cpu", &settings.isUseCpu);
    
    m_interface.addSeparator();
    m_interface.addButton("reset", [=](){
        this->tearduwnButterfly();
        this->setupButterfly();
    });
    m_interface.addParam("number of butterfly", &settings.numberOfButterfly);
    m_interface.addSeparator();

    m_interface.addParam("shader", {"single butterfly", "multiple butterfly"}, &settings.shaderType);
    m_interface.addParam("butterfly type", {"A", "B", "C", "D", "E", "F", "G", "H"}, &settings.butterflyType);
    m_interface.addSeparator();
    
    m_interface.addParam("background", &settings.backgroundColor);
    m_interface.addSeparator();
    m_interface.addButton("toggle fullscreen", [=](){
        this->setFullScreen(!this->isFullScreen());
    });
    
    try
    {
        m_shader1 = cinder::gl::GlslProg(loadResource("vert.glsl"), loadResource("frag_single.glsl"));
        m_shader2 = cinder::gl::GlslProg(loadResource("vert.glsl"), loadResource("frag.glsl"));
    }
    catch(std::exception &e)
    {
        std::cout << e.what();
    }
    
    m_time = 0.0;
    
    //butterfly polygon wave front obj
    std::string v0 = "v 0.468699 -0.337256 -0.776348\nv 0.884041 -0.556920 0.895708\nv -0.468696 -0.337256 -0.776344\nv -0.884042 -0.556920 0.895709\nv 0.000001 0.133319 0.395986\nv 0.000001 0.133319 -0.391213";
    std::string v1 = "v 0.468699 0.193708 -0.776284\nv 0.884041 0.441554 0.895676\nv -0.468696 0.193708 -0.776281\nv -0.884042 0.441554 0.895677\nv 0.000001 -0.120367 0.396050\nv 0.000001 -0.120367 -0.391149";
    std::string vt = "vt 0.494400 0.611200\nvt 0.496000 0.260800\nvt 0.784000 0.019200\nvt 0.025600 0.856000\nvt 0.220800 0.051200\nvt 0.972800 0.937600\nvt 0.494400 0.257600";
    std::string f = "f 5/1 6/2 3/3\nf 2/4 1/5 5/1\nf 4/6 5/1 3/3\nf 1/5 6/7 5/1";
    
    std::vector<ButterflyClassVertex> butterflyClassVertices;
    std::vector<Vec3f> positions0;
    std::vector<Vec3f> positions1;
    std::vector<Vec2f> texcoords;
    
    //positions0
    std::vector<std::string> lines;
    boost::algorithm::split(lines, v0, boost::algorithm::is_any_of("\n"));
    for(const std::string& line : lines)
    {
        std::vector<std::string> components;
        boost::algorithm::split(components, line, boost::algorithm::is_space());
        
        Vec3f position;
        for(int i = 0 ; i < 3 ; ++i)
        {
            position[i] = boost::lexical_cast<float, std::string>(components[i + 1]);
        }
        positions0.push_back(position);
    }
    
    //positions1
    lines.clear();
    boost::algorithm::split(lines, v1, boost::algorithm::is_any_of("\n"));
    for(const std::string& line : lines)
    {
        std::vector<std::string> components;
        boost::algorithm::split(components, line, boost::algorithm::is_space());
        
        Vec3f position;
        for(int i = 0 ; i < 3 ; ++i)
        {
            position[i] = boost::lexical_cast<float, std::string>(components[i + 1]);
        }
        positions1.push_back(position);
    }
    
    //texcoord
    lines.clear();
    boost::algorithm::split(lines, vt, boost::algorithm::is_any_of("\n"));
    for(const std::string& line : lines)
    {
        std::vector<std::string> components;
        boost::algorithm::split(components, line, boost::algorithm::is_space());
        
        Vec2f texcoord;
        for(int i = 0 ; i < 2 ; ++i)
        {
            texcoord[i] = boost::lexical_cast<float, std::string>(components[i + 1]);
        }
        texcoords.push_back(texcoord);
    }
    lines.clear();
    
    //create class vertex buffer
    boost::algorithm::split(lines, f, boost::algorithm::is_any_of("\n"));
    for(const std::string& line : lines)
    {
        std::vector<std::string> components;
        boost::algorithm::split(components, line, boost::algorithm::is_any_of(" /"));
        
        int face[6] = {0};
        for(int i = 0 ; i < sizeof(face) / sizeof(face[0]) ; ++i)
        {
            face[i] = boost::lexical_cast<int, std::string>(components[i + 1]) - 1;
        }
        
        for(int i = 0 ; i < 3 ; ++i)
        {
            ButterflyClassVertex vertex =
            {
                positions0[face[i * 2]],
                positions1[face[i * 2]],
                texcoords[face[i * 2 + 1]],
            };
            
            butterflyClassVertices.push_back(vertex);
        }
    }
    
    m_number_of_vertices_class = butterflyClassVertices.size();
    
    m_butterfly_class_vbo = gl::Vbo(GL_ARRAY_BUFFER);
    m_butterfly_class_vbo.bufferData(sizeof(ButterflyClassVertex) * butterflyClassVertices.size(), &butterflyClassVertices[0], GL_DYNAMIC_DRAW);
    
    for(int i = 0 ; i < 8 ; ++i)
    {
        std::stringstream name;
        name << boost::format("%d.png") % i;
        m_images.push_back(cinder::gl::Texture(loadImage(loadResource(name.str()))));
    }
    
    //cl
    //context bridge
    CGLContextObj glcontext = CGLGetCurrentContext();
    CGLShareGroupObj sharedgroup = CGLGetShareGroup(glcontext);
    gcl_gl_set_sharegroup(sharedgroup);
    m_clqueue = gcl_create_dispatch_queue(CL_DEVICE_TYPE_GPU, NULL);
    
    if(m_clqueue == NULL)
    {
        wowdev::Dialog dialog("failed to init GPU mode.");
        dialog.show();
        m_clqueue = gcl_create_dispatch_queue(CL_DEVICE_TYPE_CPU, NULL);
    }
    m_cpuqueue = gcl_create_dispatch_queue(CL_DEVICE_TYPE_CPU, NULL);
    m_group = dispatch_group_create();

    this->setupButterfly();
    
    
    //OpenGL status
    int maxTextureUnits;
    glGetIntegerv(GL_MAX_TEXTURE_UNITS, &maxTextureUnits);
    std::cout << boost::format("GL_MAX_TEXTURE_UNITS : %d") % maxTextureUnits << std::endl;
}
void BasicApp::resize()
{
    auto cam = m_camera.getCamera();
    cam.setPerspective( 60.0f, getWindowAspectRatio(), 1.0f, 1000.0f );
    m_camera.setCurrentCam(cam);
    
    std::cout << boost::format("resized : %dx%d") % getWindowWidth() % getWindowHeight() << std::endl;
}
void BasicApp::setupButterfly()
{
    const int WIDTH = pow(settings.numberOfButterfly, 1.0 / 3.0);
    const int HEIGHT = pow(settings.numberOfButterfly, 1.0 / 3.0);
    const int DEPTH = pow(settings.numberOfButterfly, 1.0 / 3.0);
    
    std::vector<ButterflyInstance> butterflyInstanceVertices;
    
    for(int z = 0 ; z < DEPTH ; ++z)
    {
        float zvalue = remap(z, 0, DEPTH - 1, -200, 200);
        
        for(int y = 0 ; y < HEIGHT ; ++y)
        {
            float yvalue = remap(y, 0, HEIGHT - 1, -200, 200);
            
            for(int x = 0 ; x < WIDTH ; ++x)
            {
                float xvalue = remap(x, 0, WIDTH - 1, -200, 200);
                
                ButterflyInstance v;
                Vec3f position(xvalue, yvalue, zvalue);
                Vec3f direction(0.0f, 0.0f, 1.0f);
                v.position = cl::toCLVec3f(position);
                v.direction = cl::toCLVec3f(direction);
                v.offset = remap(rand(), 0, RAND_MAX, 0, 10.0);
                v.textureIndex = rand() % 8;
                butterflyInstanceVertices.push_back(v);
            }
        }
    }
    
    m_number_of_instance = butterflyInstanceVertices.size();
    m_butterfly_instance_vbo = gl::Vbo(GL_ARRAY_BUFFER);
    m_butterfly_instance_vbo.bufferData(sizeof(ButterflyInstance) * butterflyInstanceVertices.size(), &butterflyInstanceVertices[0], GL_STATIC_DRAW);
    
    m_butterfly_instance_gpu = (ButterflyInstance *)gcl_gl_create_ptr_from_buffer(m_butterfly_instance_vbo.getId());
    dispatch_sync(m_clqueue, ^{
        m_buffetfly_velocity_gpu = (cl_float3 *)gcl_malloc(sizeof(cl_float3) * m_number_of_instance, NULL, 0);
        cl_float3 *mapped = (cl_float3 *)gcl_map_ptr(m_buffetfly_velocity_gpu, CL_MAP_WRITE, 0);
        for(int i = 0 ; i < m_number_of_instance ; ++i)
        {
            for(int j = 0 ; j < 4 ; ++j)
            {
                mapped[i].s[j] = 0.0f;
            }
        }
        gcl_unmap(mapped);
    });
}
void BasicApp::tearduwnButterfly()
{
    gcl_free(m_butterfly_instance_gpu);
    m_butterfly_instance_vbo = gl::Vbo();
}

void BasicApp::mouseMove( MouseEvent event )
{
	m_mousePos = event.getPos();
}
void BasicApp::mouseDrag( MouseEvent event )
{
    m_mousePos = event.getPos();
    m_camera.mouseDrag( event.getPos(), event.isLeftDown(), event.isMiddleDown(), event.isRightDown() );
}
void BasicApp::mouseDown(MouseEvent event)
{
    m_camera.mouseDown( event.getPos() );
}

void BasicApp::keyDown( KeyEvent event )
{
    if(event.getCode() == KeyEvent::KEY_ESCAPE)
    {
        this->quit();
    }
}

void BasicApp::draw()
{
    //manual move
    
    float u = m_mousePos.x / (float)getWindowWidth();
    float v = m_mousePos.y / (float)getWindowHeight();
    Ray ray = m_camera.getCamera().generateRay(u , 1.0f - v, m_camera.getCamera().getAspectRatio());
    
    
    Vec3f positionWithMouse;
    float rayIntersectPlaneResult;
    if(ray.calcPlaneIntersection(Vec3f::zero(), -m_camera.getCamera().getViewDirection(), &rayIntersectPlaneResult))
    {
        positionWithMouse = ray.getOrigin() + ray.getDirection() * rayIntersectPlaneResult;
    }
    
    
    if(settings.isAdditiveBlending)
    {
        gl::disableDepthRead();
        gl::disableDepthWrite();
    }
    else
    {
        gl::enableDepthRead();
        gl::enableDepthWrite();
    }

    
	gl::setMatrices( m_camera.getCamera() );
    
	gl::clear( settings.backgroundColor );
    
    gl::color( Colorf(0.6f, 0.6f, 0.6f) );
    const float size = 200.0f;
    const float step = 20.0f;
	for(float i=-size;i<=size;i+=step) {
		gl::drawLine( Vec3f(i, 0.0f, -size), Vec3f(i, 0.0f, size) );
		gl::drawLine( Vec3f(-size, 0.0f, i), Vec3f(size, 0.0f, i) );
	}
    
    const float LEN = 50.0f;
    glColor3f( 1.0f, 0.0f, 0.0f );
    gl::drawLine(Vec3f::zero(), Vec3f(LEN, 0.0f, 0.0f));
    glColor3f( 0.0f, 1.0f, 0.0f );
    gl::drawLine(Vec3f::zero(), Vec3f(0.0f, LEN, 0.0f));
    glColor3f( 0.0f, 0.0f, 1.0f );
    gl::drawLine(Vec3f::zero(), Vec3f(0.0f, 0.0f, LEN));
    
    Matrix44f proj =  m_camera.getCamera().getProjectionMatrix();
    Matrix44f view = m_camera.getCamera().getModelViewMatrix();
    
    if(settings.isPause == false)
    {
        m_time += (1.0 / 60.0);
    }
    
    cinder::gl::GlslProg& shader = settings.shaderType == SHADER_SINGLE_BUTTERFLY? m_shader1 : m_shader2;
    
    for(int i = 0 ; i < m_images.size() ; ++i)
    {
        m_images[i].bind(i);
    }
    
    //geometory instancing rendering
    
    shader.bind();
    shader.uniform("u_transform", proj * view);
    shader.uniform("u_time", static_cast<float>(m_time));
    int samplers[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    shader.uniform("u_images", samplers, sizeof(samplers) / sizeof(samplers[0]));
    shader.uniform("u_textureIndex", settings.butterflyType);
    shader.uniform("u_size", settings.size);
    
    GLuint a_position0 = shader.getAttribLocation("a_position0");
    GLuint a_position1 = shader.getAttribLocation("a_position1");
    GLuint a_texcoord = shader.getAttribLocation("a_texcoord");
    
    GLuint ai_position = shader.getAttribLocation("ai_position");
    GLuint ai_direction = shader.getAttribLocation("ai_direction");
    GLuint ai_offset = shader.getAttribLocation("ai_offset");
    GLuint ai_textureIndex = shader.getAttribLocation("ai_textureIndex");
    
    glEnableVertexAttribArray(a_position0);
    glEnableVertexAttribArray(a_position1);
    glEnableVertexAttribArray(a_texcoord);
    glEnableVertexAttribArray(ai_position);
    glEnableVertexAttribArray(ai_direction);
    glEnableVertexAttribArray(ai_offset);
    glEnableVertexAttribArray(ai_textureIndex);

    glVertexAttribDivisorARB(ai_position, 1);
    glVertexAttribDivisorARB(ai_direction, 1);
    glVertexAttribDivisorARB(ai_offset, 1);
    glVertexAttribDivisorARB(ai_textureIndex, 1);
    
    m_butterfly_class_vbo.bind();
    glVertexAttribPointer(a_position0, 3, GL_FLOAT, GL_FALSE, sizeof(ButterflyClassVertex), (char *)0 + offsetof(ButterflyClassVertex, position0));
    glVertexAttribPointer(a_position1, 3, GL_FLOAT, GL_FALSE, sizeof(ButterflyClassVertex), (char *)0 + offsetof(ButterflyClassVertex, position1));
    glVertexAttribPointer(a_texcoord, 2, GL_FLOAT, GL_FALSE, sizeof(ButterflyClassVertex), (char *)0 + offsetof(ButterflyClassVertex, texcoord));
    m_butterfly_class_vbo.unbind();
    
    m_butterfly_instance_vbo.bind();
    glVertexAttribPointer(ai_position, 3, GL_FLOAT, GL_FALSE, sizeof(ButterflyInstance), (char *)0 + offsetof(ButterflyInstance, position));
    glVertexAttribPointer(ai_direction, 3, GL_FLOAT, GL_FALSE, sizeof(ButterflyInstance), (char *)0 + offsetof(ButterflyInstance, direction));
    glVertexAttribPointer(ai_offset, 1, GL_FLOAT, GL_FALSE, sizeof(ButterflyInstance), (char *)0 + offsetof(ButterflyInstance, offset));
    glVertexAttribPointer(ai_textureIndex, 1, GL_FLOAT, GL_FALSE, sizeof(ButterflyInstance), (char *)0 + offsetof(ButterflyInstance, textureIndex));
    m_butterfly_instance_vbo.unbind();
    
    if(settings.isAdditiveBlending)
    {
        //additive
        gl::enableAlphaBlending();
        gl::enableAdditiveBlending();
    }
    else
    {
        gl::disableAlphaBlending();
    }

    
    dispatch_group_wait(m_group, DISPATCH_TIME_FOREVER);
    glDrawArraysInstancedARB(GL_TRIANGLES, 0, m_number_of_vertices_class, m_number_of_instance);
    GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    
    glDisableVertexAttribArray(a_position0);
    glDisableVertexAttribArray(a_position1);
    glDisableVertexAttribArray(a_texcoord);
    
    glDisableVertexAttribArray(ai_position);
    glDisableVertexAttribArray(ai_direction);
    glDisableVertexAttribArray(ai_offset);
    glDisableVertexAttribArray(ai_textureIndex);
    shader.unbind();
    

    float time = m_time * 0.4;
    Vec3f tracking_position;
    if(settings.isManualMove)
    {
        tracking_position = positionWithMouse;
    }
    else
    {
        tracking_position = Vec3f(sin(time * 0.5) * 250.0f, cos(time) * 250.0f, cos(time * 0.8) * 250.0f);
        tracking_position *= remap(sin(time), -1, 1, 0.3f, 1.0);
    }
    
    if(settings.isPause == false)
    {
        
        glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 1000 * 1000);
        glDeleteSync(sync);
        
        dispatch_queue_t queue = settings.isUseCpu? m_cpuqueue : m_clqueue;
        dispatch_group_async(m_group, queue, ^{
            cl_ndrange ndrange = {
                1, /*number of dimensions*/
                {0, 0, 0}, /*offset*/
                {(size_t)m_number_of_instance, 0, 0}, /*range each demensions */
                {0, 0, 0}, /*?*/
            };
            
            butterfly_main_kernel(&ndrange, m_butterfly_instance_gpu, m_buffetfly_velocity_gpu,
                                  tracking_position.x, tracking_position.y, tracking_position.z, time);
        });
    }

    if(settings.isManualMove)
    {
        gl::color(0.0f, 1.0f, 0.0f);
    }
    else
    {
        gl::color(1.0f, 1.0f, 1.0f);
    }
    gl::drawSphere(tracking_position, 3.0f);
    
    m_fps = getAverageFps();
    
    m_interface.draw();
}

// This line tells Flint to actually create the application
CINDER_APP_BASIC( BasicApp, RendererGl )