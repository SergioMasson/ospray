/********************************************************************* *\
 * INTEL CORPORATION PROPRIETARY INFORMATION                            
 * This software is supplied under the terms of a license agreement or  
 * nondisclosure agreement with Intel Corporation and may not be copied 
 * or disclosed except in accordance with the terms of that agreement.  
 * Copyright (C) 2014 Intel Corporation. All Rights Reserved.           
 ********************************************************************* */

#include "coidevice_common.h"
#include <stdio.h>
#include <sink/COIPipeline_sink.h>
#include <sink/COIProcess_sink.h>
#include <sink/COIBuffer_sink.h>
#include <common/COIMacros_common.h>
#include <common/COISysInfo_common.h>
#include <common/COIEvent_common.h>
#include <iostream>
#include "handle.h"
//ospray
#include "ospray/common/model.h"
#include "ospray/common/data.h"
#include "ospray/geometry/trianglemesh.h"
#include "ospray/camera/camera.h"
#include "ospray/volume/volume.h"
#include "ospray/render/renderer.h"
#include "ospray/render/renderer.h"
#include "ospray/render/loadbalancer.h"
#include "ospray/texture/texture2d.h"
#include "ospray/lights/light.h"
// stl
#include <algorithm>

using namespace std;

namespace ospray {
  namespace coi {

    // only used if manual buffer uploads are turned on ...
    void *uploadBuffer = NULL;

    COINATIVELIBEXPORT
    void ospray_coi_initialize(uint32_t         in_BufferCount,
                               void**           in_ppBufferPointers,
                               uint64_t*        in_pBufferLengths,
                               void*            in_pMiscData,
                               uint16_t         in_MiscDataLength,
                               void*            in_pReturnValue,
                               uint16_t         in_ReturnValueLength)
    {
      UNREFERENCED_PARAM(in_BufferCount);
      UNREFERENCED_PARAM(in_ppBufferPointers);
      UNREFERENCED_PARAM(in_pBufferLengths);
      UNREFERENCED_PARAM(in_pMiscData);
      UNREFERENCED_PARAM(in_MiscDataLength);
      UNREFERENCED_PARAM(in_ReturnValueLength);
      UNREFERENCED_PARAM(in_pReturnValue);

      int32 *deviceInfo = (int32*)in_pMiscData;
      int deviceID = deviceInfo[0];
      int numDevices = deviceInfo[1];
      ospray::debugMode = deviceInfo[2];
      ospray::logLevel = deviceInfo[3];

      if (ospray::debugMode || ospray::logLevel >= 1) {
        std::cout << "!osp:coi: initializing device #" << deviceID
                  << " (" << (deviceID+1) << "/" << numDevices << ")" << std::endl;
        COIProcessProxyFlush();
      }
      ospray::TiledLoadBalancer::instance = new ospray::InterleavedTiledLoadBalancer(deviceID,numDevices);
    }

    COINATIVELIBEXPORT
    void ospray_coi_new_model(uint32_t         numBuffers,
                              void**           bufferPtr,
                              uint64_t*        bufferSize,
                              void*            argsPtr,
                              uint16_t         argsSize,
                              void*            retVal,
                              uint16_t         retValSize)
    {
      if (ospray::debugMode || ospray::logLevel >= 1) {
        std::cout << "!osp:coi: new model" << std::endl;
        COIProcessProxyFlush();
      }
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      Model *model = new Model;
      handle.assign(model);
      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_new_trianglemesh(uint32_t         numBuffers,
                                     void**           bufferPtr,
                                     uint64_t*        bufferSize,
                                     void*            argsPtr,
                                     uint16_t         argsSize,
                                     void*            retVal,
                                     uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      TriangleMesh *mesh = new TriangleMesh;
      handle.assign(mesh);
      if (ospray::debugMode) COIProcessProxyFlush();
    }


    COINATIVELIBEXPORT
    void ospray_coi_new_data(uint32_t         numBuffers,
                             void**           bufferPtr,
                             uint64_t*        bufferSize,
                             void*            argsPtr,
                             uint16_t         argsSize,
                             void*            retVal,
                             uint16_t         retValSize)
    {

      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      int    nitems = args.get<int32>(); 
      int    format = args.get<int32>(); 
      int    flags  = args.get<int32>(); 

      void *init = bufferPtr[0];

      COIRESULT res = COIBufferAddRef(bufferPtr[0]);
      if (res != COI_SUCCESS) 
        throw std::runtime_error(std::string("error in coibufferaddref: ")
                                 +COIResultGetName(res));
      
      if (format == OSP_STRING)
        throw std::runtime_error("data arrays of strings not currently supported on coi device ...");

      if (format == OSP_OBJECT) {
        Handle *in = (Handle *)bufferPtr[0];
        ManagedObject **out = (ManagedObject **)bufferPtr[0];
        for (int i=0;i<nitems;i++) {
          if (in[i]) {
            out[i] = in[i].lookup();
            out[i]->refInc();
          } else
            out[i] = 0;
        }
        Data *data = new Data(nitems,(OSPDataType)format,
                              out,flags|OSP_DATA_SHARED_BUFFER);
        handle.assign(data);
      } else {
        Data *data = new Data(nitems,(OSPDataType)format,
                              bufferPtr[0],flags|OSP_DATA_SHARED_BUFFER);
        handle.assign(data);
      }
      
      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_upload_data_done(uint32_t         numBuffers,
                                     void**           bufferPtr,
                                     uint64_t*        bufferSize,
                                     void*            argsPtr,
                                     uint16_t         argsSize,
                                     void*            retVal,
                                     uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      int    nitems = args.get<int32>(); 
      int    format = args.get<int32>(); 
      int    flags  = args.get<int32>(); 

      if (ospray::debugMode) {
        cout << "=======================================================" << endl;
        cout << "!osp:coi: done uploading data " << handle.ID() << endl;
      }
      
      Data *data = (Data*)handle.lookup();

      size_t size = nitems * sizeOf((OSPDataType)format);

      if (format == OSP_STRING)
        throw std::runtime_error("data arrays of strings not currently supported on coi device ...");

      if (format == OSP_OBJECT) {
        if (ospray::debugMode) COIProcessProxyFlush();
        Handle *in = (Handle *)data->data;
        ManagedObject **out = (ManagedObject **)data->data;
        for (int i=0;i<nitems;i++) {
          if (in[i]) {
            out[i] = in[i].lookup();
            out[i]->refInc();
          } else
            out[i] = 0;
        }
        if (ospray::debugMode) COIProcessProxyFlush();
      }
      
      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_upload_data_chunk(uint32_t         numBuffers,
                                void**           bufferPtr,
                                uint64_t*        bufferSize,
                                void*            argsPtr,
                                uint16_t         argsSize,
                                void*            retVal,
                                uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      int64  begin = args.get<int64>(); 
      int64  size = args.get<int64>(); 

      Data *data = (Data*)handle.lookup();
      memcpy((char*)data->data + begin, uploadBuffer, size);
      
      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_create_new_empty_data(uint32_t         numBuffers,
                                   void**           bufferPtr,
                                   uint64_t*        bufferSize,
                                   void*            argsPtr,
                                   uint16_t         argsSize,
                                   void*            retVal,
                                   uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      int    nitems = args.get<int32>(); 
      int    format = args.get<int32>(); 
      int    flags  = args.get<int32>(); 

      if (ospray::debugMode) {
        cout << "=======================================================" << endl;
        cout << "!osp:coi: new (as-yet-empty) data " << handle.ID() << endl;
      }
      
      if (format == OSP_STRING)
        throw std::runtime_error("data arrays of strings not currently supported on coi device ...");

      size_t size = nitems * sizeOf((OSPDataType)format);
      void *mem = new char[size];
      bzero(mem,size);
      Data *data = new Data(nitems,(OSPDataType)format,
                            mem,flags|OSP_DATA_SHARED_BUFFER);
      handle.assign(data);
      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_new_geometry(uint32_t         numBuffers,
                                 void**           bufferPtr,
                                 uint64_t*        bufferSize,
                                 void*            argsPtr,
                                 uint16_t         argsSize,
                                 void*            retVal,
                                 uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      const char *type = args.getString();

      Geometry *geom = Geometry::createGeometry(type);
      handle.assign(geom);

      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_new_framebuffer(uint32_t         numBuffers,
                                    void**           bufferPtr,
                                    uint64_t*        bufferSize,
                                    void*            argsPtr,
                                    uint16_t         argsSize,
                                    void*            retVal,
                                    uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      vec2i size = args.get<vec2i>();
      uint32 mode = args.get<uint32>();
      uint32 channels = args.get<uint32>();

      int32 *pixelArray = (int32*)bufferPtr[0];

      FrameBuffer::ColorBufferFormat colorBufferFormat
        = (FrameBuffer::ColorBufferFormat)mode;
      bool hasDepthBuffer = (channels & OSP_FB_DEPTH)!=0;
      bool hasAccumBuffer = (channels & OSP_FB_ACCUM)!=0;
      
      FrameBuffer *fb = new LocalFrameBuffer(size,colorBufferFormat,
                                             hasDepthBuffer,hasAccumBuffer,
                                             pixelArray);
      handle.assign(fb);

      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_framebuffer_clear(uint32_t         numBuffers,
                                      void**           bufferPtr,
                                      uint64_t*        bufferSize,
                                      void*            argsPtr,
                                      uint16_t         argsSize,
                                      void*            retVal,
                                      uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle _fb = args.get<Handle>();
      FrameBuffer *fb = (FrameBuffer*)_fb.lookup();
      const uint32 channel = args.get<uint32>();
      fb->clear(channel);
    }

    COINATIVELIBEXPORT
    void ospray_coi_new_camera(uint32_t         numBuffers,
                                 void**           bufferPtr,
                                 uint64_t*        bufferSize,
                                 void*            argsPtr,
                                 uint16_t         argsSize,
                                 void*            retVal,
                                 uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      const char *type = args.getString();

      Camera *geom = Camera::createCamera(type);
      handle.assign(geom);

      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_new_volume(uint32_t         numBuffers,
                                 void**           bufferPtr,
                                 uint64_t*        bufferSize,
                                 void*            argsPtr,
                                 uint16_t         argsSize,
                                 void*            retVal,
                                 uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      const char *type = args.getString();
      if (ospray::logLevel > 0)
        cout << "!osp:coi: new volume " << handle.ID() << " " << type << endl;

      Volume *geom = Volume::createVolume(type);
      handle.assign(geom);

      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_new_volume_from_file(uint32_t         numBuffers,
                                 void**           bufferPtr,
                                 uint64_t*        bufferSize,
                                 void*            argsPtr,
                                 uint16_t         argsSize,
                                 void*            retVal,
                                 uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      const char *filename = args.getString();
      const char *type = args.getString();
      cout << "!osp:coi: new volume " << handle.ID() << " " << type << endl;

      Volume *geom = Volume::createVolume(filename, type);
      handle.assign(geom);

      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_new_transfer_function(uint32_t         numBuffers,
                                 void**           bufferPtr,
                                 uint64_t*        bufferSize,
                                 void*            argsPtr,
                                 uint16_t         argsSize,
                                 void*            retVal,
                                 uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      const char *type = args.getString();
      cout << "!osp:coi: new transfer function " << handle.ID() << " " << type << endl;

      TransferFunction *transferFunction = TransferFunction::createTransferFunction(type);
      handle.assign(transferFunction);

      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_new_renderer(uint32_t         numBuffers,
                                 void**           bufferPtr,
                                 uint64_t*        bufferSize,
                                 void*            argsPtr,
                                 uint16_t         argsSize,
                                 void*            retVal,
                                 uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      const char *type = args.getString();

      Renderer *geom = Renderer::createRenderer(type);
      handle.assign(geom);

      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_pin_upload_buffer(uint32_t         numBuffers,
                                  void**           bufferPtr,
                                  uint64_t*        bufferSize,
                                  void*            argsPtr,
                                  uint16_t         argsSize,
                                  void*            retVal,
                                  uint16_t         retValSize)
    {
      void *buffer = bufferPtr[0];
      uploadBuffer = buffer;
      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_new_material(uint32_t         numBuffers,
                                 void**           bufferPtr,
                                 uint64_t*        bufferSize,
                                 void*            argsPtr,
                                 uint16_t         argsSize,
                                 void*            retVal,
                                 uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      Handle _renderer = args.get<Handle>();
      const char *type = args.getString();

      Renderer *renderer = (Renderer*)_renderer.lookup();
      Material *mat = NULL;
      if (renderer)
        mat = renderer->createMaterial(type);
      if (!mat)
        mat = Material::createMaterial(type);
      
      if (mat) {
        *(int*)retVal = true;
        handle.assign(mat);
      } else {
        *(int*)retVal = false;
      }

      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_new_light(uint32_t         numBuffers,
                              void**           bufferPtr,
                              uint64_t*        bufferSize,
                              void*            argsPtr,
                              uint16_t         argsSize,
                              void*            retVal,
                              uint16_t         retValSize)
    {
      if (ospray::logLevel >= 2) {
        std::cout << "!osp:coi: new light" << std::endl;
        COIProcessProxyFlush();
      }
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      Handle _renderer = args.get<Handle>();
      const char *type = args.getString();
      Renderer *renderer = (Renderer *)_renderer.lookup();

      Light *light = NULL;
      if (ospray::debugMode) COIProcessProxyFlush(); 

      if (renderer)
        light = renderer->createLight(type);
      if (!light)
        light = Light::createLight(type);
      assert(light);
      handle.assign(light);

      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_new_texture2d(uint32_t         numBuffers,
                                  void**           bufferPtr,
                                  uint64_t*        bufferSize,
                                  void*            argsPtr,
                                  uint16_t         argsSize,
                                  void*            retVal,
                                  uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();
      int    width  = args.get<int32>(); 
      int    height = args.get<int32>(); 
      int    type   = args.get<int32>(); 
      int    flags  = args.get<int32>(); 

      COIBufferAddRef(bufferPtr[0]);

      Texture2D *tx = Texture2D::createTexture(width, height, (OSPDataType)type, bufferPtr[0], flags);

      handle.assign(tx);
      if (ospray::debugMode) COIProcessProxyFlush();
    }
                                
    COINATIVELIBEXPORT
    void ospray_coi_add_geometry(uint32_t         numBuffers,
                                 void**           bufferPtr,
                                 uint64_t*        bufferSize,
                                 void*            argsPtr,
                                 uint16_t         argsSize,
                                 void*            retVal,
                                 uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle model = args.get<Handle>();
      Handle geom  = args.get<Handle>();

      Model *m = (Model*)model.lookup();
      m->geometry.push_back((Geometry*)geom.lookup());
      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_set_material(uint32_t         numBuffers,
                                 void**           bufferPtr,
                                 uint64_t*        bufferSize,
                                 void*            argsPtr,
                                 uint16_t         argsSize,
                                 void*            retVal,
                                 uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle geom = args.get<Handle>();
      Handle mat  = args.get<Handle>();

      Geometry *g = (Geometry*)geom.lookup();
      g->setMaterial((Material*)mat.lookup());
      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_commit(uint32_t         numBuffers,
                           void**           bufferPtr,
                           uint64_t*        bufferSize,
                           void*            argsPtr,
                           uint16_t         argsSize,
                           void*            retVal,
                           uint16_t         retValSize)
    {
      if (ospray::debugMode || ospray::logLevel >= 1) {
        std::cout << "!osp:coi: commit" << std::endl;
        COIProcessProxyFlush();
      }
      DataStream args(argsPtr);
      Handle handle = args.get<Handle>();

      ManagedObject *obj = handle.lookup();
      Assert(obj);
      obj->commit();


      // hack, to stay compatible with earlier version
      Model *model = dynamic_cast<Model *>(obj);
      if (model) {
        model->finalize();
      }

      if (ospray::debugMode) COIProcessProxyFlush();
      if (ospray::debugMode || ospray::logLevel >= 1) {
        std::cout << "!osp:coi: DONE commit" << std::endl;
        COIProcessProxyFlush();
      }
    }

    /*! remove an existing geometry from a model */
    struct GeometryLocator {
      bool operator()(const embree::Ref<ospray::Geometry> &g) const {
        return ptr == &*g;
      }
      Geometry *ptr;
    };

    COINATIVELIBEXPORT
    void ospray_coi_remove_geometry(uint32_t         numBuffers,
                                    void**           bufferPtr,
                                    uint64_t*        bufferSize,
                                    void*            argsPtr,
                                    uint16_t         argsSize,
                                    void*            retVal,
                                    uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle _model = args.get<Handle>();
      Handle _geometry = args.get<Handle>();

      Model *model = (Model*)_model.lookup();
      Geometry *geometry = (Geometry*)_geometry.lookup();

      GeometryLocator locator;
      locator.ptr = geometry;
      Model::GeometryVector::iterator it = std::find_if(model->geometry.begin(), model->geometry.end(), locator);
      if(it != model->geometry.end()) {
        model->geometry.erase(it);
      }

      if (ospray::debugMode) COIProcessProxyFlush();
    }

    COINATIVELIBEXPORT
    void ospray_coi_render_frame(uint32_t         numBuffers,
                                 void**           bufferPtr,
                                 uint64_t*        bufferSize,
                                 void*            argsPtr,
                                 uint16_t         argsSize,
                                 void*            retVal,
                                 uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle _fb       = args.get<Handle>();
      Handle renderer = args.get<Handle>();
      uint32 channelFlags = args.get<uint32>();
      FrameBuffer *fb = (FrameBuffer*)_fb.lookup();
      Renderer *r = (Renderer*)renderer.lookup();
      r->renderFrame(fb,channelFlags);
    }

    COINATIVELIBEXPORT
    void ospray_coi_render_frame_sync(uint32_t         numBuffers,
                                      void**           bufferPtr,
                                      uint64_t*        bufferSize,
                                      void*            argsPtr,
                                      uint16_t         argsSize,
                                      void*            retVal,
                                      uint16_t         retValSize)
    {
      // currently all rendering is synchronous, anyway ....
    }

    COINATIVELIBEXPORT
    void ospray_coi_set_value(uint32_t         numBuffers,
                              void**           bufferPtr,
                              uint64_t*        bufferSize,
                              void*            argsPtr,
                              uint16_t         argsSize,
                              void*            retVal,
                              uint16_t         retValSize)
    {
      DataStream args(argsPtr);
      Handle target = args.get<Handle>();
      const char *name = args.getString();
      ManagedObject *obj = target.lookup();
      if (!obj) return;
    
      Assert(obj);

      OSPDataType type = args.get<OSPDataType>();

      switch (type) {
      case OSP_INT : obj->set(name,args.get<int32>()); break;
      case OSP_INT2: obj->set(name,args.get<vec2i>()); break;
      case OSP_INT3: obj->set(name,args.get<vec3i>()); break;
      case OSP_INT4: obj->set(name,args.get<vec4i>()); break;

      case OSP_UINT : obj->set(name,args.get<uint32>()); break;
      case OSP_UINT2: obj->set(name,args.get<vec2ui>()); break;
      case OSP_UINT3: obj->set(name,args.get<vec3ui>()); break;
      case OSP_UINT4: obj->set(name,args.get<vec4ui>()); break;

      case OSP_FLOAT : obj->set(name,args.get<float>()); break;
      case OSP_FLOAT2: obj->set(name,args.get<vec2f>()); break;
      case OSP_FLOAT3: obj->set(name,args.get<vec3f>()); break;
      case OSP_FLOAT4: obj->set(name,args.get<vec4f>()); break;
        
      case OSP_STRING: obj->set(name,args.getString()); break;
      case OSP_OBJECT: obj->set(name,args.get<Handle>().lookup()); break;

      default:
        throw "ospray_coi_set_value no timplemented for given data type";
      }
    }
  }
}

int main(int ac, const char **av)
{
  ospray::init(&ac,&av);
  if (ospray::logLevel >= 1)
    printf("!osp:coi: ospray_coi_worker starting up.\n");

  // initialize embree. (we need to do this here rather than in
  // ospray::init() because in mpi-mode the latter is also called
  // in the host-stubs, where it shouldn't.
  std::stringstream embreeConfig;
  embreeConfig << "verbose=" << ospray::logLevel;
  if (ospray::debugMode) 
    embreeConfig << ",threads=1";
  rtcInit(embreeConfig.str().c_str());
  //rtcInit("verbose=2,traverser=single,threads=1");
  //rtcInit("verbose=2,traverser=chunk");

  assert(rtcGetError() == RTC_NO_ERROR);
  ospray::TiledLoadBalancer::instance = NULL;

  COIPipelineStartExecutingRunFunctions();
  if (ospray::debugMode) COIProcessProxyFlush();
  COIProcessWaitForShutdown();
}
  
