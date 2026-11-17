#include <GLFW/glfw3.h>
#ifdef _WIN32
  #define NOMINMAX
  #include <windows.h>
  #include <mmsystem.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

struct Vec3 { double x, y, z; };
struct Meteor { Vec3 p, v; float life, maxLife, brightness; };
static double length(Vec3 v) { return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); }

struct Body {
    const char* name;
    double orbit, radius, period, tilt;
    float r, g, b;
    bool ring;
    double axialTilt, spin;
    float atmosphere;
    const char* detail;
};

class Audio {
    std::string path;
    bool muted = false;

    void tone(double hz, int milliseconds) {
        if (muted) return;
        std::thread([=, filePath=path] {
            const int sampleRate = 44100;
            const int samples = sampleRate * milliseconds / 1000;
            std::ofstream out(path, std::ios::binary);
            if (!out) return;
            int dataSize = samples * 2;
            int riffSize = 36 + dataSize;
            short format = 1, channels = 1, bits = 16, blockAlign = 2;
            int subchunkSize = 16, byteRate = sampleRate * 2;
            out.write("RIFF", 4); out.write(reinterpret_cast<const char*>(&riffSize), 4);
            out.write("WAVEfmt ", 8); out.write(reinterpret_cast<const char*>(&subchunkSize), 4);
            out.write(reinterpret_cast<const char*>(&format), 2); out.write(reinterpret_cast<const char*>(&channels), 2);
            out.write(reinterpret_cast<const char*>(&sampleRate), 4); out.write(reinterpret_cast<const char*>(&byteRate), 4);
            out.write(reinterpret_cast<const char*>(&blockAlign), 2); out.write(reinterpret_cast<const char*>(&bits), 2);
            out.write("data", 4); out.write(reinterpret_cast<const char*>(&dataSize), 4);
            for (int i = 0; i < samples; ++i) {
                double fadeIn = std::min(1.0, i / 400.0);
                double fadeOut = std::min(1.0, (samples - i) / 2200.0);
                double envelope = fadeIn * fadeOut;
                short sample = static_cast<short>(std::sin(6.283185307 * hz * i / sampleRate) * 14000 * envelope);
                out.write(reinterpret_cast<const char*>(&sample), 2);
            }
            out.close();
#ifdef _WIN32
            PlaySoundA(filePath.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
#endif
        }).detach();
    }

public:
    explicit Audio(std::string filePath) : path(std::move(filePath)) {}
    void toggle() { muted = !muted; if (!muted) tone(700, 90); }
    bool isMuted() const { return muted; }
    void select() { tone(540, 90); }
    void click() { tone(280, 55); }
};

class SolarApp {
    GLFWwindow* window = nullptr;
    int width = 1280, height = 820;
    bool paused = false, labels = true, trails = true, dragging = false, follow = false, infoPanel = true;
    bool pointerOnButton = false;
    GLFWcursor* handCursor = nullptr; GLFWcursor* arrowCursor = nullptr; double elapsed = 0;
    bool running = true;
    double simDay = 0, speed = 1.0, yaw = 0.48, pitch = -0.36, zoom = 1.0;
    double lastMouseX = 0, lastMouseY = 0;
    int selected = 0, hovered = -1;
    Audio audio;
    std::vector<Body> bodies;
    std::vector<Vec3> stars;
    std::vector<Vec3> asteroids;
    std::vector<Meteor> meteors;
    std::mt19937 rng{42};

    static SolarApp* self(GLFWwindow* w) { return static_cast<SolarApp*>(glfwGetWindowUserPointer(w)); }

    static void framebufferSizeCallback(GLFWwindow* w, int width, int height) {
        auto* app = self(w); app->width = std::max(1, width); app->height = std::max(1, height);
        glViewport(0, 0, app->width, app->height);
    }

    static void cursorCallback(GLFWwindow* w, double x, double y) {
        auto* app = self(w);
        app->pointerOnButton = app->buttonAt(x,y) >= 0;
        if (!app->dragging) {
            app->hovered = app->pointerOnButton ? -1 : app->pickPlanet(x, y);
            app->lastMouseX = x; app->lastMouseY = y; if(app->handCursor) glfwSetCursor(w, (app->pointerOnButton || app->hovered>=0) ? app->handCursor : app->arrowCursor); return;
        }
        app->yaw += (x - app->lastMouseX) * 0.008;
        app->pitch += (y - app->lastMouseY) * 0.008;
        app->pitch = std::clamp(app->pitch, -1.35, 1.35);
        app->lastMouseX = x; app->lastMouseY = y;
    }

    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int) {
        auto* app = self(w);
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            double mx,my; glfwGetCursorPos(w,&mx,&my); int ui=app->buttonAt(mx,my);
            if(ui>=0){app->action(ui); app->dragging=false; return;}
            app->dragging = true;
            glfwGetCursorPos(w, &app->lastMouseX, &app->lastMouseY);
            int picked = app->pickPlanet(app->lastMouseX, app->lastMouseY);
            if (picked >= 0) { app->selected = picked; app->audio.select(); }
        }
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) app->dragging = false;
    }

    static void scrollCallback(GLFWwindow* w, double, double y) {
        auto* app = self(w);
        app->zoom = std::clamp(app->zoom * (y > 0 ? 1.12 : 1.0 / 1.12), 0.45, 2.8);
        app->audio.click();
    }

    static void keyCallback(GLFWwindow* w, int key, int, int action, int) {
        if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
        auto* app = self(w);
        if (key == GLFW_KEY_ESCAPE) app->running = false;
        else if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) { app->paused = !app->paused; app->audio.click(); }
        else if (key == GLFW_KEY_R && action == GLFW_PRESS) { app->simDay = 0; app->yaw = .48; app->pitch = -.36; app->zoom = 1; app->audio.click(); }
        else if (key == GLFW_KEY_L && action == GLFW_PRESS) { app->labels = !app->labels; app->audio.click(); }
        else if (key == GLFW_KEY_T && action == GLFW_PRESS) { app->trails = !app->trails; app->audio.click(); }
        else if (key == GLFW_KEY_M && action == GLFW_PRESS) app->audio.toggle();
        else if (key == GLFW_KEY_F && action == GLFW_PRESS) { app->follow = !app->follow; app->audio.click(); }
        else if (key == GLFW_KEY_I && action == GLFW_PRESS) { app->infoPanel = !app->infoPanel; app->audio.click(); }
        else if ((key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) && action == GLFW_PRESS) app->speed = std::min(20.0, app->speed * 1.4);
        else if ((key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) && action == GLFW_PRESS) app->speed = std::max(.1, app->speed / 1.4);
        else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_8 && action == GLFW_PRESS) { app->selected = key - GLFW_KEY_1; app->audio.select(); }
    }

public:
    SolarApp()
        : audio(
#ifdef _WIN32
            std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\solaris_tone.wav"
#else
            "/tmp/solaris_tone.wav"
#endif
          ),
          bodies{
              {"MERCURY", 55, 5, 88, 0, .60f, .62f, .66f, false, .03, 1.0, 0.00f, "Smallest planet; heavily cratered and closest to the Sun."},
              {"VENUS", 82, 8, 225, 3, .88f, .62f, .25f, false, 177.4, -0.25, .30f, "Hot, cloud-covered world with a dense carbon-dioxide atmosphere."},
              {"EARTH", 116, 9, 365, 7, .20f, .42f, .95f, false, 23.4, 1.0, .24f, "Our home world, with liquid oceans and a protective atmosphere."},
              {"MARS", 154, 7, 687, 2, .90f, .18f, .14f, false, 25.2, .97, .06f, "The red planet, marked by iron-rich dust and ancient valleys."},
              {"JUPITER", 220, 20, 4333, 1, .88f, .46f, .16f, false, 3.1, 2.4, .03f, "Gas giant with powerful storms and the Great Red Spot."},
              {"SATURN", 300, 17, 10759, 3, .95f, .75f, .30f, true, 26.7, 2.2, .02f, "Gas giant surrounded by a bright, icy ring system."},
              {"URANUS", 370, 13, 30687, 4, .22f, .75f, .85f, false, 97.8, 1.4, .04f, "An ice giant rotating on its side with a blue methane haze."},
              {"NEPTUNE", 435, 13, 60190, 5, .18f, .35f, .95f, false, 28.3, 1.5, .04f, "Distant ice giant with supersonic winds and dark storms.\n"}
          } {}

    bool init() {
        if (!glfwInit()) { std::cerr << "GLFW initialization failed\n"; return false; }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        window = glfwCreateWindow(width, height, "Solaris // Interactive Solar System", nullptr, nullptr);
        if (!window) { std::cerr << "Could not create GLFW window\n"; glfwTerminate(); return false; }
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
        glfwSetCursorPosCallback(window, cursorCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetScrollCallback(window, scrollCallback);
        glfwSetKeyCallback(window, keyCallback);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_POINT_SMOOTH);
        glShadeModel(GL_SMOOTH);
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        glEnable(GL_NORMALIZE);
        GLfloat ambient[] = {0.035f, 0.035f, 0.055f, 1.0f};
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
        GLfloat specular[] = {0.35f, 0.35f, 0.35f, 1.0f};
        glLightfv(GL_LIGHT0, GL_SPECULAR, specular);

        std::uniform_real_distribution<double> random(-1, 1);
        for (int i = 0; i < 1800; ++i) {
            Vec3 s{random(rng), random(rng), random(rng)};
            double l = length(s); if (l < .2) { --i; continue; }
            stars.push_back({s.x * 900 / l, s.y * 900 / l, s.z * 900 / l});
        }
        std::uniform_real_distribution<double> beltAngle(0, 6.283185307);
        std::uniform_real_distribution<double> beltRadius(174, 195);
        std::uniform_real_distribution<double> beltHeight(-3.0, 3.0);
        for (int i = 0; i < 950; ++i) {
            double a = beltAngle(rng), radius = beltRadius(rng);
            asteroids.push_back({radius * std::cos(a), beltHeight(rng), radius * std::sin(a)});
        }
        for(int i=0;i<32;++i) meteors.push_back(newMeteor());
        handCursor=glfwCreateStandardCursor(GLFW_HAND_CURSOR); arrowCursor=glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        return true;
    }

    ~SolarApp() {
        if(handCursor) glfwDestroyCursor(handCursor);
        if(arrowCursor) glfwDestroyCursor(arrowCursor);
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
    }

    Vec3 planetPosition(const Body& b) const {
        double a = simDay * 6.283185307 / b.period;
        double tilt = b.tilt * 0.0174532925;
        return {b.orbit * std::cos(a), b.orbit * std::sin(tilt) * std::sin(a), b.orbit * std::sin(a)};
    }

    Vec3 moonPosition() const {
        Vec3 earth = planetPosition(bodies[2]);
        double a = simDay * 6.283185307 / 27.3;
        return {earth.x + 20 * std::cos(a), earth.y + 2 * std::sin(a), earth.z + 20 * std::sin(a)};
    }

    bool project(Vec3 p, double& screenX, double& screenY, double& depth, double scale = 1) const {
        double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
        double x1 = cy * p.x - sy * p.z, z1 = sy * p.x + cy * p.z;
        double y1 = cp * p.y - sp * z1, z2 = sp * p.y + cp * z1 + 900;
        if (z2 < 30) return false;
        double f = 700 * zoom * scale / z2;
        screenX = width / 2.0 + x1 * f;
        screenY = height / 2.0 - y1 * f;
        depth = z2;
        return true;
    }

    int pickPlanet(double mx, double my) const {
        int best = -1; double bestDistance = 25;
        for (int i = 0; i < static_cast<int>(bodies.size()); ++i) {
            double x, y, z; if (!project(planetPosition(bodies[i]), x, y, z)) continue;
            double radius = std::max(4.0, bodies[i].radius * zoom * 530 / z);
            double distance = std::hypot(x - mx, y - my);
            if (distance < std::max(bestDistance, radius + 5)) { best = i; bestDistance = distance; }
        }
        return best;
    }

    void set3DProjection() const {
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        double aspect = static_cast<double>(width) / height;
        double nearPlane = 1, farPlane = 2200, fov = 55.0 * 0.0174532925;
        double top = std::tan(fov / 2) * nearPlane;
        glFrustum(-top * aspect, top * aspect, -top, top, nearPlane, farPlane);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glTranslated(0, 0, -900);
        glRotated(-pitch * 57.2957795, 1, 0, 0);
        glRotated(-yaw * 57.2957795, 0, 1, 0);
        glScaled(zoom, zoom, zoom);
    }

    void sphere(Vec3 p, double radius, float r, float g, float b, double axialTilt = 0, double rotation = 0, int style = -1) const {
        glPushMatrix(); glTranslated(p.x, p.y, p.z);
        glRotated(axialTilt, 0, 0, 1); glRotated(rotation, 0, 1, 0); glColor3f(r, g, b);
        constexpr int slices = 36, stacks = 22;
        for (int j = 0; j < stacks; ++j) {
            double lat0 = -1.5707963 + 3.1415926 * j / stacks;
            double lat1 = -1.5707963 + 3.1415926 * (j + 1) / stacks;
            glBegin(GL_QUAD_STRIP);
            for (int i = 0; i <= slices; ++i) {
                double lon = 6.2831853 * i / slices;
                for (double lat : {lat0, lat1}) {
                    double c = std::cos(lat); glNormal3d(c * std::cos(lon), std::sin(lat), c * std::sin(lon));
                    float f=1.f; double n=std::sin(lat*19+std::sin(lon*5)*2.1)+.45*std::sin(lat*37+lon*8);
                    if(style==0) f=float(.82+.10*n+.06*std::sin(lon*31+lat*23));
                    else if(style==1) f=float(.86+.09*std::sin(lat*11+std::sin(lon*3))+.04*n);
                    else if(style==2){double land=std::sin(lon*3.1+std::sin(lat*8))*std::cos(lat*7)+.35*std::sin(lon*9+lat*11);f=land>.12?1.12f:.78f;if(std::abs(lat)<.12)f=1.18f;}
                    else if(style==3) f=float(.77+.17*n+.08*std::sin(lon*15+lat*12));
                    else if(style==4||style==5) f=float(.88+.11*std::sin(lat*(style==4?24:16)+.45*std::sin(lon*4))+.03*n);
                    else if(style==6) f=float(.90+.08*std::sin(lat*13+std::sin(lon*6))+.04*n);
                    else if(style==7) f=float(.84+.14*std::sin(lat*16+std::sin(lon*4))+.05*n);
                    float rr=std::clamp(r*f,0.f,1.f),gg=std::clamp(g*f,0.f,1.f),bb=std::clamp(b*f,0.f,1.f);
                    if(style==2){double land=std::sin(lon*3.1+std::sin(lat*8))*std::cos(lat*7)+.35*std::sin(lon*9+lat*11);if(std::abs(lat)<.12){rr=gg=bb=.88f;}else if(land>.12){rr=.25f;gg=.39f;bb=.18f;}else{rr=.07f;gg=.27f;bb=.61f;}}
                    if(style==7 && lat<.18 && lat>-.38 && lon>2.2 && lon<2.7){rr=.10f;gg=.18f;bb=.31f;}
                    glColor3f(rr,gg,bb);
                    glVertex3d(radius * c * std::cos(lon), radius * std::sin(lat), radius * c * std::sin(lon));
                }
            }
            glEnd();
        }
        glPopMatrix();
    }

    void atmosphere(Vec3 p, double radius, float r, float g, float b) const {
        // Restrained camera-facing halo: unlike an equatorial outline this cannot read as a ring.
        GLboolean light=glIsEnabled(GL_LIGHTING),depth=glIsEnabled(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
        double x,y,z; if(project(p,x,y,z)){ glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0,width,height,0,-1,1); glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
          float rr=float(std::clamp(radius*700*zoom/z*1.35,4.0,42.0)); glColor4f(r,g,b,.10f); glBegin(GL_TRIANGLE_FAN); glVertex2d(x,y); for(int i=0;i<=40;++i){double a=6.2831853*i/40;glVertex2d(x+rr*cos(a),y+rr*sin(a));} glEnd(); glPopMatrix(); glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW); }
        if(depth) glEnable(GL_DEPTH_TEST);
        if(light) glEnable(GL_LIGHTING);
    }

    void selectionMarker(Vec3 p, double radius, bool strong) const {
        double x,y,z;if(!project(p,x,y,z))return; GLboolean depth=glIsEnabled(GL_DEPTH_TEST),light=glIsEnabled(GL_LIGHTING);glDisable(GL_DEPTH_TEST);glDisable(GL_LIGHTING);
        glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,width,height,0,-1,1);glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
        double q=std::clamp(radius*700*zoom/z+7.0,9.0,30.0),a=q*.4;glColor4f(1,1,1,strong?1.f:.7f);glLineWidth(strong?2.f:1.3f);glBegin(GL_LINES);for(int sx:{-1,1})for(int sy:{-1,1}){double cx=x+sx*q,cy=y+sy*q;glVertex2d(cx-sx*a,cy);glVertex2d(cx,cy);glVertex2d(cx,cy-sy*a);glVertex2d(cx,cy);}glEnd();glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);if(depth)glEnable(GL_DEPTH_TEST);if(light)glEnable(GL_LIGHTING);
    }

    void orbit(const Body& b) const {
        if (!trails) return;
        glDisable(GL_LIGHTING); glLineWidth(1.2f); glColor4f(.92f, .94f, .98f, .9f); glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 160; ++i) {
            double a = 6.2831853 * i / 160;
            glVertex3d(b.orbit * std::cos(a), b.orbit * std::sin(b.tilt * .0174533) * std::sin(a), b.orbit * std::sin(a));
        }
        glEnd(); glEnable(GL_LIGHTING);
    }

    void drawStars() const {
        GLboolean light=glIsEnabled(GL_LIGHTING);glDisable(GL_LIGHTING);glDepthMask(GL_FALSE);
        for(int bucket=0;bucket<2;++bucket){glPointSize(bucket?2.6f:1.6f);glBegin(GL_POINTS);for(std::size_t i=0;i<stars.size();++i){if((i%23==0)!=(bucket==1))continue;const auto&s=stars[i];float q=(i%13==0)?1.f:.58f+float((i*37)%41)/100.f;float blue=(i%7==0)?1.f:.9f;glColor3f(q,q*.94f,std::min(1.f,q*blue));glVertex3d(s.x,s.y,s.z);}glEnd();}
        glDepthMask(GL_TRUE);if(light)glEnable(GL_LIGHTING);
    }

    void drawAsteroids() const {
        GLboolean light=glIsEnabled(GL_LIGHTING);glDisable(GL_LIGHTING);for(int group=0;group<3;++group){glPointSize(1.25f+group*.55f);glBegin(GL_POINTS);for(std::size_t i=0;i<asteroids.size();++i){if(int(i%3)!=group)continue;const auto&a=asteroids[i];float k=.30f+float((i*17)%52)/100.f;glColor3f(k,k*.82f,k*.68f);glVertex3d(a.x,a.y,a.z);}glEnd();}for(std::size_t i=0;i<asteroids.size();i+=12){const auto&a=asteroids[i];float k=.32f+float((i*19)%42)/100.f,s=.65f+float(i%5)*.16f;glPushMatrix();glTranslated(a.x,a.y,a.z);glColor3f(k,k*.82f,k*.66f);glBegin(GL_TRIANGLES);glVertex3f(0,s,0);glVertex3f(-s,-s*.4f,-s*.5f);glVertex3f(s*.6f,-s*.6f,-s*.5f);glVertex3f(0,s,0);glVertex3f(s*.6f,-s*.6f,-s*.5f);glVertex3f(0,-s*.5f,s);glVertex3f(0,s,0);glVertex3f(0,-s*.5f,s);glVertex3f(-s,-s*.4f,-s*.5f);glVertex3f(-s,-s*.4f,-s*.5f);glVertex3f(0,-s*.5f,s);glVertex3f(s*.6f,-s*.6f,-s*.5f);glEnd();glPopMatrix();}if(light)glEnable(GL_LIGHTING);
    }

    void drawSaturnRing(const Body& b) const {
        Vec3 p = planetPosition(b); glPushMatrix(); glTranslated(p.x, p.y, p.z); glRotated(b.axialTilt, 0, 0, 1); glRotated(63.3,1,0,0); glDisable(GL_LIGHTING);glDepthMask(GL_FALSE);
        const float bands[4][4]={{.78f,.69f,.54f,.58f},{.92f,.83f,.67f,.72f},{.58f,.52f,.43f,.62f},{.84f,.75f,.60f,.70f}};
        for(int j=0;j<4;++j){double inner=b.radius*(1.25+j*.35),outer=inner+b.radius*.35;glColor4fv(bands[j]);glBegin(GL_QUAD_STRIP);for(int i=0;i<=120;++i){double a=6.2831853*i/120;glVertex3d(cos(a)*inner,0,sin(a)*inner);glVertex3d(cos(a)*outer,0,sin(a)*outer);}glEnd();} glDepthMask(GL_TRUE);glEnable(GL_LIGHTING);glPopMatrix();
    }

    Meteor newMeteor(){std::uniform_real_distribution<double>d(-1,1),v(45,95),life(1.4,4.0);Vec3 p{d(rng)*560,d(rng)*400,-600-d(rng)*450};Vec3 vel{v(rng)*(d(rng)>.2?1:-1),v(rng)*d(rng),v(rng)*.25};float l=float(life(rng));return{p,vel,l,l,float(.65+std::abs(d(rng))*.35)};}
    void updateMeteors(double dt){for(auto&m:meteors){m.life-=float(dt);m.p.x+=m.v.x*dt;m.p.y+=m.v.y*dt;m.p.z+=m.v.z*dt;if(m.life<=0||length(m.p)>1200)m=newMeteor();}}
    void drawMeteors()const{GLboolean light=glIsEnabled(GL_LIGHTING);glDisable(GL_LIGHTING);glDepthMask(GL_FALSE);glBlendFunc(GL_SRC_ALPHA,GL_ONE);for(auto&m:meteors){float f=std::clamp(m.life/m.maxLife,0.f,1.f)*m.brightness;glLineWidth(2);glBegin(GL_LINES);glColor4f(1,.36f,.12f,f*.06f);glVertex3d(m.p.x-m.v.x*.14,m.p.y-m.v.y*.14,m.p.z-m.v.z*.14);glColor4f(1,.9f,.72f,f);glVertex3d(m.p.x,m.p.y,m.p.z);glEnd();}glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_TRUE);if(light)glEnable(GL_LIGHTING);}
    void action(int id){switch(id){case 0:paused=!paused;audio.click();break;case 1:simDay=0;yaw=.48;pitch=-.36;zoom=1;audio.click();break;case 2:labels=!labels;audio.click();break;case 3:trails=!trails;audio.click();break;case 4:audio.toggle();break;case 5:follow=!follow;audio.click();break;case 6:infoPanel=!infoPanel;audio.click();break;}}
    int buttonAt(double x,double y)const{float bw=std::clamp((float(width)-32.f)/2.f,88.f,140.f),left=std::max(8.f,float(width)-2*bw-24.f),top=std::max(78.f,float(height)-210.f),bh=32;for(int i=0;i<7;++i){int col=i%2,row=i/2;float bx=left+col*(bw+8.f),by=top+row*39;if(x>=bx&&x<=bx+bw&&y>=by&&y<=by+bh)return i;}return -1;}
    void drawButtons()const{float bw=std::clamp((float(width)-32.f)/2.f,88.f,140.f),left=std::max(8.f,float(width)-2*bw-24.f),top=std::max(78.f,float(height)-210.f),bh=32;const char* names[7]={paused?"PLAY":"PAUSE","RESET",labels?"LABELS ON":"LABELS OFF",trails?"ORBITS ON":"ORBITS OFF",audio.isMuted()?"SOUND OFF":"SOUND ON",follow?"FOLLOW ON":"FOLLOW OFF",infoPanel?"INFO ON":"INFO OFF"};const float col[7][3]={{.05f,.68f,.36f},{.9f,.18f,.16f},{.05f,.55f,.76f},{.04f,.47f,.72f},{.32f,.37f,.44f},{.9f,.48f,.12f},{.82f,.44f,.12f}};GLdouble mx,my;glfwGetCursorPos(window,&mx,&my);for(int i=0;i<7;++i){float x=left+(i%2)*(bw+8.f),y=top+(i/2)*39;bool hot=mx>=x&&mx<=x+bw&&my>=y&&my<=y+bh;glColor4f(.025f,.04f,.065f,.98f);glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+bw,y);glVertex2f(x+bw,y+bh);glVertex2f(x,y+bh);glEnd();glColor4f(std::min(1.f,col[i][0]+(hot?.18f:0)),std::min(1.f,col[i][1]+(hot?.18f:0)),std::min(1.f,col[i][2]+(hot?.18f:0)),.92f);glBegin(GL_QUADS);glVertex2f(x+2,y+2);glVertex2f(x+bw-2,y+2);glVertex2f(x+bw-2,y+bh-2);glVertex2f(x+2,y+bh-2);glEnd();float tw=float(std::string(names[i]).size())*6.0f;text(x+(bw-tw)/2,y+12,names[i],1,1,1,1);}}

    const unsigned char* glyph(char c) const {
        static const unsigned char blank[7] = {0,0,0,0,0,0,0};
        static const unsigned char digits[10][7] = {{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,2,12}};
        static const unsigned char letters[26][7] = {{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,14},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
        if (c >= '0' && c <= '9') return digits[c - '0'];
        if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
        return blank;
    }

    void text(float x, float y, const std::string& value, float scale, float r, float g, float b) const {
        glColor3f(r, g, b); glBegin(GL_QUADS);
        for (char c : value) {
            if(c >= 'a' && c <= 'z') c=char(c-'a'+'A');
            const auto* bitmap = glyph(c);
            for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col)
                if (bitmap[row] & (1 << (4 - col))) {
                    float px = x + col * scale, py = y + row * scale;
                    glVertex2f(px, py); glVertex2f(px + scale, py); glVertex2f(px + scale, py + scale); glVertex2f(px, py + scale);
                }
            x += 6 * scale;
        }
        glEnd();
    }
    void textWrapped(float x,float y,const std::string& value,float scale,float maxWidth,float r,float g,float b) const {
        float lineY=y; std::string line;
        for(const std::string& word : [&](){std::vector<std::string> words;std::string w;for(char c:value){if(c==' '||c=='\n'){if(!w.empty()){words.push_back(w);w.clear();}}else w.push_back(c);}if(!w.empty())words.push_back(w);return words;}()) {
            std::string next=line.empty()?word:line+" "+word;
            if(!line.empty()&&next.size()*6*scale>maxWidth){text(x,lineY,line,scale,r,g,b);lineY+=9*scale;line=word;}else line=next;
        }
        if(!line.empty())text(x,lineY,line,scale,r,g,b);
    }

    void drawHud() const {
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glDisable(GL_DEPTH_TEST);
        glColor4f(.035f, .055f, .095f, .96f); glBegin(GL_QUADS); glVertex2f(0,0); glVertex2f(width,0); glVertex2f(width,72); glVertex2f(0,72); glEnd();
        text(26, 18, "SOLARIS", 3, 1.0f, .63f, .2f); text(27, 45, "INTERACTIVE SYSTEM", 1.6f, .4f, .5f, .65f);
        text(width - 235, 18, paused ? "PAUSED" : "LIVE", 2, paused ? 1.f : .3f, paused ? .75f : .9f, .35f);
        char info[80]; std::snprintf(info, sizeof(info), "DAY %06.1f  SPEED %3.1fx", simDay, speed); text(width - 235, 44, info, 1.5f, .9f, .93f, 1.f);
        text(26, height - 58, "SPACE PAUSE   R RESET   L LABELS   T ORBITS   M SOUND   F FOLLOW", 1.4f, .4f, .5f, .65f);
        text(26, height - 34, "DRAG ROTATE   WHEEL ZOOM   1-8 SELECT   I INFO   ESC QUIT", 1.4f, .4f, .5f, .65f);
        std::string focus = "FOCUS  "; focus += bodies[selected].name; text(26, 96, focus, 1.8f, .9f, .93f, 1.f);
        if (labels) {
            for (int i = 0; i < static_cast<int>(bodies.size()); ++i) {
                double x, y, z;
                if (project(planetPosition(bodies[i]), x, y, z)) {
                    if(buttonAt(x,y)>=0 || (infoPanel && x>width-320 && y<255) || (x>width-320 && y>height-245) || y>height-70) continue;
                    float cr= i==selected||i==hovered?1.f:.90f; float sc=i==selected?1.7f:1.35f;
                    glColor4f(.015f,.022f,.04f,.82f); glBegin(GL_QUADS); float bw=float(std::string(bodies[i].name).size())*6*sc+6; glVertex2f(float(x+4),float(y-8));glVertex2f(float(x+4)+bw,float(y-8));glVertex2f(float(x+4)+bw,float(y-8)+7*sc+4);glVertex2f(float(x+4),float(y-8)+7*sc+4);glEnd(); text(static_cast<float>(x + 7), static_cast<float>(y - 4), bodies[i].name, sc, cr,cr,cr);
                }
            }
        }
        if (infoPanel) {
            float left = static_cast<float>(width - 300), top = 92, right = static_cast<float>(width - 20), bottom = 250;
            glColor4f(.035f, .055f, .095f, .92f); glBegin(GL_QUADS);
            glVertex2f(left, top); glVertex2f(right, top); glVertex2f(right, bottom); glVertex2f(left, bottom); glEnd();
            const auto& b = bodies[selected];
            text(left + 16, top + 16, b.name, 2.1f, 1, 1, 1);
            char stats[100]; std::snprintf(stats, sizeof(stats), "ORBIT %3.0f  ROTATION %3.1fx", b.orbit, b.spin); text(left + 16, top + 48, stats, 1.25f, .72f, .78f, .9f);
            std::snprintf(stats, sizeof(stats), "AXIAL TILT %4.1f DEG", b.axialTilt); text(left + 16, top + 70, stats, 1.25f, .72f, .78f, .9f);
            text(left + 16, top + 99, "DETAIL", 1.25f, 1.f, .7f, .25f);
            textWrapped(left + 16, top + 122, b.detail, .78f, 250.f, .78f, .84f, .92f);
        }
        drawButtons();
        glEnable(GL_DEPTH_TEST); glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
    }

    void draw() const {
        glClearColor(.005f, .008f, .018f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        set3DProjection();
        GLfloat lightPosition[] = {0.f, 0.f, 0.f, 1.f}; glLightfv(GL_LIGHT0, GL_POSITION, lightPosition);
        drawStars(); drawAsteroids(); drawMeteors(); for (const auto& b : bodies) orbit(b);
        GLfloat sunEmission[]={.95f,.30f,.025f,1.f};glMaterialfv(GL_FRONT,GL_EMISSION,sunEmission);glDisable(GL_LIGHTING); sphere({0,0,0}, 30, 1.f, .47f, .08f); sphere({-8, 8, -2}, 12, 1.f, .8f, .18f);glEnable(GL_LIGHTING);GLfloat noEmission[]={0,0,0,1};glMaterialfv(GL_FRONT,GL_EMISSION,noEmission);
        std::vector<std::pair<double, int>> order;
        for (int i = 0; i < static_cast<int>(bodies.size()); ++i) { double x,y,z; if (project(planetPosition(bodies[i]), x, y, z)) order.push_back({z, i}); }
        std::sort(order.rbegin(), order.rend());
        for (auto [depth, i] : order) {
            const auto& b = bodies[i]; Vec3 p = planetPosition(b);
            sphere(p, b.radius, b.r, b.g, b.b, b.axialTilt, simDay * b.spin * 0.25, i);
            if (i==1) atmosphere(p,b.radius,.98f,.66f,.28f); else if(i==2) atmosphere(p,b.radius,.20f,.55f,1.f); else if(i==6) atmosphere(p,b.radius,.32f,.82f,.92f); else if(i==7) atmosphere(p,b.radius,.22f,.42f,1.f);
            if (b.ring) drawSaturnRing(b);
            if (i == selected) selectionMarker(p, b.radius, true);
            else if (i == hovered) selectionMarker(p, b.radius, false);
        }
        Vec3 moon = moonPosition(); sphere(moon, 3.0, .64f, .66f, .70f, 6.7, simDay * 0.4, 0);
        if (selected == 2 || hovered == 2) selectionMarker(moon, 3.0, selected == 2);
        drawHud(); glfwSwapBuffers(window);
    }

    void run() {
        if (!init()) return;
        auto previous = std::chrono::steady_clock::now();
        while (running && !glfwWindowShouldClose(window)) {
            glfwPollEvents();
            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - previous).count(); previous = now;
            elapsed += dt; updateMeteors(std::min(dt,.1)); if (!paused) simDay += dt * 45 * speed;
            if (follow) { Vec3 p = planetPosition(bodies[selected]); yaw = std::atan2(p.z, p.x) - 1.57; }
            draw();
        }
    }
};

int main() {
    SolarApp app;
    app.run();
    return 0;
}
