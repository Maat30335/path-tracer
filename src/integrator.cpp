#include "integrator.h"
#include <fstream>
#include <iostream>
#include "interaction.h"
#include "useful.h"
#include "point.h"
#include "vector.h"
#include "disney.h"
#include <thread>
#include <vector>

void SamplerIntegrator::ScanlineRender(int start, int end, const Scene &scene) const{
    int imageWidth = camera->resolution.x;
    std::unique_ptr<PixelSampler> threadSampler = sampler->Clone();
    double scale = 1. / threadSampler->samplesPerPixel;
    for(int j = start; j < end; j++){
        std::cout << "Scanline... " << j << std::endl;
        for(int i = 0; i < imageWidth; i++){
            threadSampler->StartPixel();
            Color c{0, 0, 0};
            for(int k = 0; k < threadSampler->samplesPerPixel; k++){
                Point2f sample = threadSampler->Get2D();
                Ray r = camera->GenerateRay(sample, Point2i(i, j));
                c = c + rayColor(r, scene, 10);
            }
            c = c * scale;
            film->WriteColor(c, Point2i(i, j));
        }
    }
}

void SamplerIntegrator::Render(const Scene &scene) const{
    int imageHeight = camera->resolution.y;
    int imageWidth = camera->resolution.x;
    double scale = 1. / sampler->samplesPerPixel;
    for (int j = imageHeight-1; j >= 0; --j) {
        std::cout << "Scanline... " << j << std::endl;
        for (int i = 0; i < imageWidth; ++i) {
            sampler->StartPixel();
            Color c{0, 0, 0};
            for(int k = 0; k < sampler->samplesPerPixel; k++){
                Point2f sample = sampler->Get2D();
                Ray r = camera->GenerateRay(sample, Point2i(i, j));
                c = c + rayColor(r, scene, 10);
            }
            c = c * scale;
            film->WriteColor(c, Point2i(i, j));
        }
    }
    film->WriteFile("test.ppm");
}

void SamplerIntegrator::MultiRender(const Scene &scene) const{
    int imageHeight = camera->resolution.y;
    const int numThreads = 16;
    std::vector<std::thread> threads;
    const int chunk_size = imageHeight / numThreads;
    int leftover = imageHeight % numThreads;
    int start = 0;
    int end;
    for(int i = 0; i < numThreads; i++){
        end = start + chunk_size;
        if (leftover > 0) {
            end++;
            leftover--;
        }
        threads.emplace_back(&SamplerIntegrator::ScanlineRender, this, start, end, std::ref(scene));
        start = end;
    }
    for (auto& thread : threads){
        thread.join();
    }
    film->WriteFile("test.ppm");
}

Color RedIntegrator::rayColor(const Ray &r, const Scene &scene, int depth) const{
    SurfaceInteraction isect;
    if(scene.scene->Intersect(r, &isect)){
                return Color{1, 0, 0};
            }else{
                return Color{0, 0, 1};
            }
}

Color LambertIntegrator::rayColor(const Ray &r, const Scene &scene, int depth) const{
    Color L{0, 0, 0};
    Color T{1, 1, 1};

    Ray ray = r;

    for(int i = 0; i < depth; i++){
        SurfaceInteraction isect;
        if(scene.scene->Intersect(ray, &isect)){
                Vector3f wi = Normalize((Vector3f)isect.n + Normalize(randomInSphere()));
                Vector3f wo = Normalize(ray.d);
                T *= 0.5;
                ray = Ray(isect.p + Epsilon * (Vector3f)isect.n, wi);
        }else{
                
                Vector3f unit_direction = Normalize(ray.d);
                auto t = 0.5*(unit_direction.y + 1.0);
                L = T * ((1.0-t)*Color(1.0, 1.0, 1.0) + t*Color(0.5, 0.7, 1.0));
                return L;
        }
    }

    return L;

}


Color DisneyIntegrator::rayColor(const Ray &r, const Scene &scene, int depth) const{


    Color L{0, 0, 0};
    Color T{1, 1, 1};

    Ray ray = r;
    double oldPDF = 0;
    bool oldDirac = true;

    for(int i = 0; i < depth; i++){
        SurfaceInteraction isect;
        if(scene.scene->Intersect(ray, &isect)){
            BSDFSample sample;
            Vector3f wo = Normalize(-ray.d);
            sample = Disney::SampleDisney(isect, wo);
            Vector3f wi = sample.wi;
            if(sample.isBlack){
                    return Color(0, 0, 0);
            }
            Point3f pHit = sample.hitIn ? isect.p + Epsilon * -(Vector3f)isect.n : isect.p + Epsilon * (Vector3f)isect.n;
            bool isDirac = (1 - isect.parameters.metallic) * (1 - isect.parameters.specTrans) == 0. && isect.parameters.roughness <= 0.01;

            if(isect.light){
                if(Dot(wo, isect.n) >= 0.){
                    Color emit = oldDirac ? isect.light->getEmittance() : isect.light->getEmittance() * oldPDF / (oldPDF + isect.light->pdf(wi, ray.o, pHit));
                    /*
                        std::cout << "BSDF sample" << std::endl;
                        std::cout << "BSDF PDF: " << oldPDF << std::endl;
                        std::cout << "Light PDF: " << isect.light->pdf(wi, ray.o, pHit) << std::endl << std::endl;
                    */
                    
                    L += T * emit * scene.lights.size();
                }
                return L;
                // std::cout << "light?: " << L << std::endl;

            } else if(!scene.lights.empty() && !isDirac){ // sampling light
                // std::cout << "isect.p.y: " << isect.p.y << std::endl;
                int index = (int)(random_double() * scene.lights.size());
                LightSample lSample = scene.lights[index]->Sample_Li(isect.p);
                Ray lightRay{pHit, lSample.wi, lSample.tHit - 0.01};
                /*
                SurfaceInteraction lisect;
                std::cout << "light ray: " << lightRay << std::endl;
                std::cout << "intersect?: " << scene.scene->Intersect(lightRay, &lisect) << std::endl;
                std::cout << "lisect.p: " << lisect.p << std::endl;
                std::cout << "max ray: " << lightRay(lSample.tHit - 0.01) << std::endl;
                */

                if(!lSample.isBlack && !scene.scene->IntersectP(lightRay)){
                    bool fakeDirac = (1 - isect.parameters.metallic) * (1 - isect.parameters.specTrans) == 0. && isect.parameters.roughness <= 0.05; // delete later
                    double pdf;
                    Color t = Disney::EvaluateDisney(isect, lSample.wi, wo, &pdf);

                    /*
                    if(fakeDirac){
                        std::cout << "fakedirac moment" << std::endl;
                        std::cout << "t: " << t << std::endl;
                        std::cout << "pdf: " << pdf << std::endl;
                        std::cout << "light pdf: " << lSample.pdf << std::endl << std::endl;
                    }
                    */

                        /*
                        std::cout << "light sample" << std::endl;
                        std::cout << "t: " << t << std::endl;
                        std::cout << "BSDF PDF: " << pdf << std::endl;
                        std::cout << "Light PDF: " << lSample.pdf << std::endl << std::endl;
                        */

                    L += T * ((t * lSample.emittance) / (pdf + lSample.pdf)) * scene.lights.size();
                }

            }
            
            T = T * sample.reflectance;
            oldDirac = sample.isDirac;
            oldPDF = sample.pdf;
            ray = Ray(pHit, wi);
        }else{
            Vector3f unit_direction = Normalize(ray.d);
                auto t = 0.5*(unit_direction.y + 1.0);
                // L += T * ((1.0-t)*Color(1.0, 1.0, 1.0) + t*Color(0.5, 0.7, 1.0));
                return L;
        }
    }
    return L;
}

    






/*
    if(depth <= 0){
        return Color(0, 0, 0);
    }
    SurfaceInteraction isect;
    if(scene.scene->Intersect(r, &isect)){
                BSDFSample sample;
                Vector3f wo = Normalize(-r.d);
                sample = Disney::SampleDisney(isect, wo);
                Vector3f wi = sample.wi;
                if(sample.isBlack){
                    return Color(0, 0, 0);
                }
                Point3f pHit = sample.hitIn ? isect.p + Epsilon * -(Vector3f)isect.n : isect.p + Epsilon * (Vector3f)isect.n;
                return sample.reflectance * rayColor(Ray(pHit, wi), scene, depth - 1);
            }else{

                Vector3f unit_direction = Normalize(r.d);
                auto t = 0.5*(unit_direction.y + 1.0);
                return (1.0-t)*Color(1.0, 1.0, 1.0) + t*Color(0.5, 0.7, 1.0);
            }

}
*/


Color NormalIntegrator::rayColor(const Ray &r, const Scene &scene, int depth) const{
    SurfaceInteraction isect;
    Ray ray = r;
        if(scene.scene->Intersect(ray, &isect)){
                isect.n = Normalize(isect.n);
                return 0.5 * Color{isect.n.x + 1, isect.n.y + 1, isect.n.z + 1};
            }else{
                Vector3f unit_direction = Normalize(ray.d);
                auto t = 0.5*(unit_direction.y + 1.0);
                return (1.0-t)*Color(1.0, 1.0, 1.0) + t*Color(0.5, 0.7, 1.0);
            }
}
