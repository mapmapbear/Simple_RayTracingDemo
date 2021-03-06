#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#include "vec3.h"

#if defined __linux__ || defined __APPLE__
// "Compiled for Linux
#else
// Windows doesn't define these values by default, Linux does
#define M_PI 3.141592653589793
#define INFINITY 1e8
#endif

using Vec3f = Vec3<float>;

class Sphere {
 public:
  Vec3f center;                       /// 球的位置
  float radius, radius2;              /// 球的半径和半径^2
  Vec3f surfaceColor, emissionColor;  /// 表面颜色和发射（光）
  float transparency, reflection;     /// 表面透明度和反射率
  Sphere(const Vec3f &c, const float &r, const Vec3f &sc, const float &refl = 0,
         const float &transp = 0, const Vec3f &ec = 0)
      : center(c),
        radius(r),
        radius2(r * r),
        surfaceColor(sc),
        emissionColor(ec),
        transparency(transp),
        reflection(refl) { /* empty */
  }

  //[comment]
  // 使用几何解计算射线-球体相交
  //[/comment]
  bool intersect(const Vec3f &rayorig, const Vec3f &raydir, float &t0,
                 float &t1) const {
    Vec3f l = center - rayorig;
    float tca = l.dot(raydir);
    if (tca < 0) return false;
    float d2 = l.dot(l) - tca * tca;
    if (d2 > radius2) return false;
    float thc = sqrt(radius2 - d2);
    t0 = tca - thc;
    t1 = tca + thc;

    return true;
  }
};

//[comment]
// 此变量控制最大递归深度
//[/comment]
#define MAX_RAY_DEPTH 5

float mix(const float &a, const float &b, const float &mix) {
  return b * mix + a * (1 - mix);
}

//[comment]
/*
这是主要的跟踪功能。它接受一条射线作为参数（由它的起源和方向定义）。
我们测试这条光线是否与场景中的任何几何体相交。 如果光线与对象相交，
我们会计算交点、交点处的法线，并使用此信息对该点进行着色。 着色取决
于表面属性（它是透明的、反射的、漫反射的）。 该函数返回光线的颜色。
如果光线与对象相交，则该对象是该对象在交点处的颜色，否则返回背景颜色。
*/
//[/comment]
Vec3f trace(const Vec3f &rayorig, const Vec3f &raydir,
            const std::vector<Sphere> &spheres, const int &depth) {
  // if (raydir.length() != 1) std::cerr << "Error " << raydir << std::endl;
  float tnear = INFINITY;
  const Sphere *sphere = NULL;
  // find intersection of this ray with the sphere in the scene
  for (unsigned i = 0; i < spheres.size(); ++i) {
    float t0 = INFINITY, t1 = INFINITY;
    if (spheres[i].intersect(rayorig, raydir, t0, t1)) {
      if (t0 < 0) t0 = t1;
      if (t0 < tnear) {
        tnear = t0;
        sphere = &spheres[i];
      }
    }
  }
  // if there's no intersection return black or background color
  if (!sphere) return Vec3f(2);
  Vec3f surfaceColor =
      0;  // color of the ray/surfaceof the object intersected by the ray
  Vec3f phit = rayorig + raydir * tnear;  // point of intersection
  Vec3f nhit = phit - sphere->center;     // normal at the intersection point
  nhit.normalize();                       // normalize normal direction
  // If the normal and the view direction are not opposite to each other
  // reverse the normal direction. That also means we are inside the sphere so
  // set the inside bool to true. Finally reverse the sign of IdotN which we
  // want positive.
  float bias =
      1e-4;  // add some bias to the point from which we will be tracing
  bool inside = false;
  if (raydir.dot(nhit) > 0) nhit = -nhit, inside = true;
  if ((sphere->transparency > 0 || sphere->reflection > 0) &&
      depth < MAX_RAY_DEPTH) {
    float facingratio = -raydir.dot(nhit);
    // change the mix value to tweak the effect
    float fresneleffect = mix(pow(1 - facingratio, 3), 1, 0.1);
    // compute reflection direction (not need to normalize because all vectors
    // are already normalized)
    Vec3f refldir = raydir - nhit * 2 * raydir.dot(nhit);
    refldir.normalize();
    Vec3f reflection = trace(phit + nhit * bias, refldir, spheres, depth + 1);
    Vec3f refraction = 0;
    // if the sphere is also transparent compute refraction ray (transmission)
    if (sphere->transparency) {
      float ior = 1.1,
            eta = (inside) ? ior
                           : 1 / ior;  // are we inside or outside the surface?
      float cosi = -nhit.dot(raydir);
      float k = 1 - eta * eta * (1 - cosi * cosi);
      Vec3f refrdir = raydir * eta + nhit * (eta * cosi - sqrt(k));
      refrdir.normalize();
      refraction = trace(phit - nhit * bias, refrdir, spheres, depth + 1);
    }
    // the result is a mix of reflection and refraction (if the sphere is
    // transparent)
    surfaceColor = (reflection * fresneleffect +
                    refraction * (1 - fresneleffect) * sphere->transparency) *
                   sphere->surfaceColor;
  } else {
    // it's a diffuse object, no need to raytrace any further
    for (unsigned i = 0; i < spheres.size(); ++i) {
      if (spheres[i].emissionColor.x > 0) {
        // this is a light
        Vec3f transmission = 1;
        Vec3f lightDirection = spheres[i].center - phit;
        lightDirection.normalize();
        for (unsigned j = 0; j < spheres.size(); ++j) {
          if (i != j) {
            float t0, t1;
            if (spheres[j].intersect(phit + nhit * bias, lightDirection, t0,
                                     t1)) {
              transmission = 0;
              break;
            }
          }
        }
        surfaceColor += sphere->surfaceColor * transmission *
                        std::max(float(0), nhit.dot(lightDirection)) *
                        spheres[i].emissionColor;
      }
    }
  }

  return surfaceColor + sphere->emissionColor;
}

//[comment]
// Main rendering function. We compute a camera ray for each pixel of the image
// trace it and return a color. If the ray hits a sphere, we return the color of
// the sphere at the intersection point, else we return the background color.
//[/comment]
void render(const std::vector<Sphere> &spheres) {
  // unsigned width = 680, height = 420;
  unsigned width = 7680, height = 4360;
  Vec3f *image = new Vec3f[width * height], *pixel = image;
  float invWidth = 1 / float(width), invHeight = 1 / float(height);
  float fov = 50, aspectratio = width / float(height);
  float angle = tan(M_PI * 0.5 * fov / 180.);
  // Trace rays
  for (unsigned y = 0; y < height; ++y) {
    for (unsigned x = 0; x < width; ++x, ++pixel) {
      float xx = (2 * ((x + 0.5) * invWidth) - 1) * angle * aspectratio;
      float yy = (1 - 2 * ((y + 0.5) * invHeight)) * angle;
      Vec3f raydir(xx, yy, -1);
      raydir.normalize();
      *pixel = trace(Vec3f(0), raydir, spheres, 0);
    }
  }
  // Save result to a PPM image (keep these flags if you compile under Windows)
  std::ofstream ofs("./untitled5.ppm", std::ios::out | std::ios::binary);
  ofs << "P6\n" << width << " " << height << "\n255\n";
  for (unsigned i = 0; i < width * height; ++i) {
    ofs << (unsigned char)(std::min(float(1), image[i].x) * 255)
        << (unsigned char)(std::min(float(1), image[i].y) * 255)
        << (unsigned char)(std::min(float(1), image[i].z) * 255);
  }
  ofs.close();
  delete[] image;
}

//[comment]
// In the main function, we will create the scene which is composed of 5 spheres
// and 1 light (which is also a sphere). Then, once the scene description is
// complete we render that scene, by calling the render() function.
//[/comment]
int main(int /*argc*/, char ** /*argv*/) {
  srand48(13);
  std::vector<Sphere> spheres;
  spheres.push_back(Sphere(Vec3f(0.0, -10004, -20), 10000, Vec3f(0.2, 0.2, 0.2),
                           0, 0.0));  //灰色
  spheres.push_back(
      Sphere(Vec3f(0.0, 0, -20), 4, Vec3f(1.00, 0.0, 0.0), 1, 0.5));
  spheres.push_back(
      Sphere(Vec3f(5.0, -1, -15), 2, Vec3f(0.0, 1.00, 0.0), 1, 0.0));
  spheres.push_back(
      Sphere(Vec3f(5.0, 0, -25), 3, Vec3f(1.0, 1.0, 0.0), 1, 0.0));
  spheres.push_back(
      Sphere(Vec3f(-5.5, 0, -15), 3, Vec3f(0.00, 1.00, 1.00), 1, 0.0));
  // light
  spheres.push_back(Sphere(Vec3f(0.0, 20, -30), 3, Vec3f(0.00, 0.00, 0.00), 0,
                           0.0, Vec3f(5)));
  render(spheres);

  return 0;
}