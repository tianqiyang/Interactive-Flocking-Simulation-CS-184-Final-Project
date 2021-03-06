#include <iostream>
#include <fstream>
#include <nanogui/nanogui.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include "misc/getopt.h" // getopt for windows
#else
#include <getopt.h>
#include <unistd.h>
#endif
#include <unordered_set>
#include <stdlib.h> // atoi for getopt inputs

#include "CGL/CGL.h"
#include "collision/plane.h"
#include "collision/cylinder.h"
#include "collision/sphere.h"
#include "flock.h"
#include "flockSimulator.h"
#include "json.hpp"
#include "misc/file_utils.h"

typedef uint32_t gid_t;

using namespace std;
using namespace nanogui;

using json = nlohmann::json;

#define msg(s) cerr << "[Flocks] " << s << endl;

FlockSimulator* app = nullptr;
GLFWwindow* window = nullptr;
Screen* screen = nullptr;

void error_callback(int error, const char* description) {
	puts(description);
}

void createGLContexts() {
    if (!glfwInit()) {
        return;
    }
    glfwSetTime(0);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_RED_BITS, 8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);

    // Create a GLFWwindow object
    window = glfwCreateWindow(800, 800, "Flcok Simulator", nullptr, nullptr);
    if (window == nullptr) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        throw std::runtime_error("Could not initialize GLAD!");
    }
    glGetError(); // pull and ignore unhandled errors like GL_INVALID_ENUM

    glClearColor(0.2f, 0.25f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Create a nanogui screen and pass the glfw pointer to initialize
    screen = new Screen();
    screen->initialize(window, true);

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
    glfwSwapInterval(1);
    glfwSwapBuffers(window);
}

void setGLFWCallbacks() {
    glfwSetCursorPosCallback(window, [](GLFWwindow*, double x, double y) {
        if (!screen->cursorPosCallbackEvent(x, y)) {
            app->cursorPosCallbackEvent(x / screen->pixelRatio(),
                y / screen->pixelRatio());
        }
        });

    glfwSetMouseButtonCallback(
        window, [](GLFWwindow*, int button, int action, int modifiers) {
            if (!screen->mouseButtonCallbackEvent(button, action, modifiers) ||
                action == GLFW_RELEASE) {
                app->mouseButtonCallbackEvent(button, action, modifiers);
            }
        });

    glfwSetKeyCallback(
        window, [](GLFWwindow*, int key, int scancode, int action, int mods) {
            if (!screen->keyCallbackEvent(key, scancode, action, mods)) {
                app->keyCallbackEvent(key, scancode, action, mods);
            }
        });

    glfwSetCharCallback(window, [](GLFWwindow*, unsigned int codepoint) {
        screen->charCallbackEvent(codepoint);
        });

    glfwSetDropCallback(window,
        [](GLFWwindow*, int count, const char** filenames) {
            screen->dropCallbackEvent(count, filenames);
            app->dropCallbackEvent(count, filenames);
        });

    glfwSetScrollCallback(window, [](GLFWwindow*, double x, double y) {
        if (!screen->scrollCallbackEvent(x, y)) {
            app->scrollCallbackEvent(x, y);
        }
        });

    glfwSetFramebufferSizeCallback(window,
        [](GLFWwindow*, int width, int height) {
            screen->resizeCallbackEvent(width, height);
            app->resizeCallbackEvent(width, height);
        });
}

// TODO: Fill in usage error messages
void usageError(const char* binaryName) {
    printf("Usage: %s [options]\n", binaryName);
    printf("Required program options:\n");
    printf("  -f     <STRING>    Filename of scene\n");
    printf("  -r     <STRING>    Project root.\n");
    printf("                     Should contain \"shaders/Default.vert\".\n");
    printf("                     Automatically searched for by default.\n");
    printf("  -a     <INT>       Sphere vertices latitude direction.\n");
    printf("  -o     <INT>       Sphere vertices longitude direction.\n");
    printf("\n");
    exit(-1);
}

void incompleteObjectError(const char* object, const char* attribute) {
    cout << "Incomplete " << object << " definition, missing " << attribute << endl;
    exit(-1);
}

const string SPHERE = "sphere";
const string PLANE = "plane";
const string CLOTH = "cloth";
const string CYLINDERS = "cylinders";
const string HCYLINDER = "hcylinder";

const unordered_set<string> VALID_KEYS = {SPHERE, PLANE, CLOTH, CYLINDERS};

// TODO: may need later
bool loadObjectsFromFile(string filename, Flock* flock, FlockParameters* fp, vector<CollisionObject*>* objects, int sphere_num_lat, int sphere_num_lon) {
  // Read JSON from file
  ifstream i(filename);
  if (!i.good()) {
    return false;
  }
  json j;
  i >> j;

  // Loop over objects in scene
  for (json::iterator it = j.begin(); it != j.end(); ++it) {
    string key = it.key();

    // Check that object is valid
    unordered_set<string>::const_iterator query = VALID_KEYS.find(key);
    if (query == VALID_KEYS.end()) {
      cout << "Invalid scene object found: " << key << endl;
      exit(-1);
    }

    // Retrieve object
    json object = it.value();

    // Parse object depending on type (flock, sphere, or plane)
    if (key == CLOTH) {
      // Cloth
      double width, height;
      int num_width_points, num_height_points;
      float thickness;
      e_orientation orientation;
      vector<vector<int>> pinned;

      auto it_width = object.find("width");
      if (it_width != object.end()) {
        width = *it_width;
      } else {
        incompleteObjectError("flock", "width");
      }

      auto it_height = object.find("height");
      if (it_height != object.end()) {
        height = *it_height;
      } else {
        incompleteObjectError("flock", "height");
      }

      auto it_num_width_points = object.find("num_width_points");
      if (it_num_width_points != object.end()) {
        num_width_points = *it_num_width_points;
      } else {
        incompleteObjectError("flock", "num_width_points");
      }

      auto it_num_height_points = object.find("num_height_points");
      if (it_num_height_points != object.end()) {
        num_height_points = *it_num_height_points;
      } else {
        incompleteObjectError("flock", "num_height_points");
      }

      auto it_thickness = object.find("thickness");
      if (it_thickness != object.end()) {
        thickness = *it_thickness;
      } else {
        incompleteObjectError("flock", "thickness");
      }

      auto it_orientation = object.find("orientation");
      if (it_orientation != object.end()) {
        orientation = *it_orientation;
      } else {
        incompleteObjectError("flock", "orientation");
      }

      auto it_pinned = object.find("pinned");
      if (it_pinned != object.end()) {
        vector<json> points = *it_pinned;
        for (auto pt : points) {
          vector<int> point = pt;
          pinned.push_back(point);
        }
      }

      flock->width = width;
      flock->height = height;
      flock->num_width_points = num_width_points;
      flock->num_height_points = num_height_points;
      flock->thickness = thickness;
      flock->orientation = orientation;
      flock->pinned = pinned;

      // Cloth parameters
      bool enable_structural_constraints, enable_shearing_constraints, enable_bending_constraints;
      double damping, density, ks;

      auto it_enable_structural = object.find("enable_structural");
      if (it_enable_structural != object.end()) {
        enable_structural_constraints = *it_enable_structural;
      } else {
        incompleteObjectError("flock", "enable_structural");
      }

      auto it_enable_shearing = object.find("enable_shearing");
      if (it_enable_shearing != object.end()) {
        enable_shearing_constraints = *it_enable_shearing;
      } else {
        incompleteObjectError("flock", "it_enable_shearing");
      }

      auto it_enable_bending = object.find("enable_bending");
      if (it_enable_bending != object.end()) {
        enable_bending_constraints = *it_enable_bending;
      } else {
        incompleteObjectError("flock", "it_enable_bending");
      }

      auto it_damping = object.find("damping");
      if (it_damping != object.end()) {
        damping = *it_damping;
      } else {
        incompleteObjectError("flock", "damping");
      }

      auto it_density = object.find("density");
      if (it_density != object.end()) {
        density = *it_density;
      } else {
        incompleteObjectError("flock", "density");
      }

      auto it_ks = object.find("ks");
      if (it_ks != object.end()) {
        ks = *it_ks;
      } else {
        incompleteObjectError("flock", "ks");
      }

    //   fp->coherence = coherence;
    //   fp->alignment = alignment;
    //   fp->separation = separation;
    } else if (key == SPHERE) {
      Vector3D origin;
      double radius, friction;

      auto it_origin = object.find("origin");
      if (it_origin != object.end()) {
        vector<double> vec_origin = *it_origin;
        origin = Vector3D(vec_origin[0], vec_origin[1], vec_origin[2]);
      } else {
        incompleteObjectError("sphere", "origin");
      }

      auto it_radius = object.find("radius");
      if (it_radius != object.end()) {
        radius = *it_radius;
      } else {
        incompleteObjectError("sphere", "radius");
      }

      auto it_friction = object.find("friction");
      if (it_friction != object.end()) {
        friction = *it_friction;
      } else {
        incompleteObjectError("sphere", "friction");
      }

      Sphere *s = new Sphere(origin, radius, friction, sphere_num_lat, sphere_num_lon);
      objects->push_back(s);
    } else if (key == PLANE) {
      Vector3D point1, point2, point3, point4, normal;
      double friction;

      auto it_point1 = object.find("point1");
      if (it_point1 != object.end()) {
        vector<double> vec_point1 = *it_point1;
        point1 = Vector3D(vec_point1[0], vec_point1[1], vec_point1[2]);
      } else {
        incompleteObjectError("plane", "point1");
      }
      
      auto it_point2 = object.find("point2");
      if (it_point2 != object.end()) {
        vector<double> vec_point2 = *it_point2;
        point2 = Vector3D(vec_point2[0], vec_point2[1], vec_point2[2]);
      } else {
        incompleteObjectError("plane", "point2");
      }
      
      auto it_point3 = object.find("point3");
      if (it_point3 != object.end()) {
        vector<double> vec_point3 = *it_point3;
        point3 = Vector3D(vec_point3[0], vec_point3[1], vec_point3[2]);
      } else {
        incompleteObjectError("plane", "point3");
      }
      
      auto it_point4 = object.find("point4");
      if (it_point4 != object.end()) {
        vector<double> vec_point4 = *it_point4;
        point4 = Vector3D(vec_point4[0], vec_point4[1], vec_point4[2]);
      } else {
        incompleteObjectError("plane", "point4");
      }

      auto it_normal = object.find("normal");
      if (it_normal != object.end()) {
        vector<double> vec_normal = *it_normal;
        normal = Vector3D(vec_normal[0], vec_normal[1], vec_normal[2]);
      } else {
        incompleteObjectError("plane", "normal");
      }

      auto it_friction = object.find("friction");
      if (it_friction != object.end()) {
        friction = *it_friction;
      } else {
        incompleteObjectError("plane", "friction");
      }

      Plane *p = new Plane(point1, point2, point3, point4, normal, friction);
      objects->push_back(p);
    } else if (key == CYLINDERS) {
      vector<double> radius, halfLength;
      double friction;
      int slices, branchNum, poleNum;
      vector<Vector3D> points;
      vector<vector<double> > rotates;

      auto it_point1 = object.find("points");
      if (it_point1 != object.end()) {
        vector<vector<double> > vec_point1 = *it_point1;
        for (vector<double> v : vec_point1) {
          points.push_back(Vector3D(v[0], v[1], v[2]));
        }
      } else {
        incompleteObjectError("cylinder", "points");
      }

      auto it_rotates = object.find("rotates");
      if (it_rotates != object.end()) {
        vector<vector<double> > temp  = *it_rotates;
        for (vector<double> v : temp) {
          vector<double> temp2{ v[0], v[1] };
          rotates.push_back(temp2);
        }
      } else {
        incompleteObjectError("cylinder", "rotates");
      }

      auto it_radius = object.find("radius");
      if (it_radius != object.end()) {
        vector<double> temp  = *it_radius;
        for(double d : temp) {
          radius.push_back(d);
        }
      } else {
        incompleteObjectError("cylinder", "radius");
      }

      auto it_halfLength = object.find("halfLengthes");
      if (it_halfLength != object.end()) {
        vector<double> temp  = *it_halfLength;
        for(double d : temp) {
          halfLength.push_back(d);
        }
      } else {
        incompleteObjectError("cylinder", "halfLengthes");
      }

      auto it_slices = object.find("slices");
      if (it_slices != object.end()) {
        slices = *it_slices;
      } else {
        incompleteObjectError("cylinder", "slices");
      }

      auto it_friction = object.find("friction");
      if (it_friction != object.end()) {
        friction = *it_friction;
      } else {
        incompleteObjectError("cylinder", "friction");
      }

      auto it_branchNum = object.find("branchNum");
      if (it_branchNum != object.end()) {
        branchNum = *it_branchNum;
      } else {
        incompleteObjectError("cylinder", "branchNum");
      }

      auto it_poleNum = object.find("poleNum");
      if (it_poleNum != object.end()) {
        poleNum = *it_poleNum;
      } else {
        incompleteObjectError("cylinder", "poleNum");
      }

      Cylinder *p = new Cylinder(points, rotates, radius, halfLength, slices, friction, branchNum, poleNum);
      objects->push_back(p);
    }
  }

  i.close();
  
  return true;
}


// May need change later
//check the search path is valid by finding search_path/shaders/shabi.txt
bool is_valid_project_root(const std::string& search_path) {
    std::stringstream ss;
    ss << search_path;
    ss << "/";
    ss << "shaders/shabi.txt"; 

    return FileUtils::file_exists(ss.str());
}

// Attempt to locate the project root automatically
bool find_project_root(const std::vector<std::string>& search_paths, std::string& retval) {

    for (std::string search_path : search_paths) {
        if (is_valid_project_root(search_path)) {
            retval = search_path;
            return true;
        }
    }
    return false;
}


int main(int argc, char** argv) {
    std::vector<std::string> search_paths = {
    ".",
    "..",
    "../..",
    "../../.."
    };
    std::string project_root;
    bool found_project_root = find_project_root(search_paths, project_root);


/* 
TODO: could used to initialize instances of flocks or birds, if their attributes are needed to be set in the following section.
*/
    Flock flock;
    FlockParameters fp;
    vector<CollisionObject*> objects;

    int c;

    int sphere_num_lat = 40;
    int sphere_num_lon = 40;

    std::string file_to_load_from;
    bool file_specified = false;


//TODO: Figure out what arguments are needed for our project.
while ((c = getopt(argc, argv, "f:r:a:o:")) != -1) {
    switch (c) {
    case 'f': {
        file_to_load_from = optarg;
        file_specified = true;
        break;
    }
    case 'r': {
        project_root = optarg;
        if (!is_valid_project_root(project_root)) {
            std::cout << "Warn: Could not find required file \"shaders/Default.vert\" in specified project root: " << project_root << std::endl;
        }
        found_project_root = true;
        break;
    }
    case 'a': {
        int arg_int = atoi(optarg);
        if (arg_int < 1) {
            arg_int = 1;
        }
        sphere_num_lat = arg_int;
        break;
    }
    case 'o': {
        int arg_int = atoi(optarg);
        if (arg_int < 1) {
            arg_int = 1;
        }
        sphere_num_lon = arg_int;
        break;
    }
    default: {
        usageError(argv[0]);
        break;
    }
    }
}

if (!found_project_root) {
    std::cout << "Error: Could not find required file \"shaders/Default.vert\" anywhere!" << std::endl;
    return -1;
}
else {
    std::cout << "Loading files starting from123: " << project_root << std::endl;

}

// TODO: write a json file and put its path in def_name
if (!file_specified) { // No arguments, default initialization
    std::stringstream def_fname;
    def_fname << project_root;
    def_fname << "/scene/pinned2.json";
    file_to_load_from = def_fname.str();
}
std::cout << "beforeload";
bool success = loadObjectsFromFile(file_to_load_from, &flock, &fp, &objects, sphere_num_lat, sphere_num_lon);
if (!success) {
    std::cout << "Warn: Unable to load from file: " << file_to_load_from << std::endl;
}
else {
    std::cout << "success";
}

glfwSetErrorCallback(error_callback);

createGLContexts();

// Initialize the Flock object
flock.buildGrid();
//flock.buildFlockMesh();

// Initialize the FlockSimulator object
app = new FlockSimulator(project_root, screen);
app->loadFlock(&flock);
app->loadFlockParameters(&fp);
app->loadCollisionObjects(&objects);
app->init();

// Call this after all the widgets have been defined

screen->setVisible(true);
screen->performLayout();

// Attach callbacks to the GLFW window

setGLFWCallbacks();

while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    glClearColor(0.25f, 0.25f, 0.25f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    app->drawContents();

    // Draw nanogui
    screen->drawContents();
    screen->drawWidgets();

    glfwSwapBuffers(window);

    if (!app->isAlive()) {
        glfwSetWindowShouldClose(window, 1);
    }
}

return 0;
}
