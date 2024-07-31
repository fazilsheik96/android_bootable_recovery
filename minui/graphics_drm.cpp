/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * DRM based mode setting test program
 * Copyright 2008 Tungsten Graphics
 *   Jakob Bornecrantz <jakob@tungstengraphics.com>
 * Copyright 2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "graphics_drm.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>

#include <android-base/macros.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>
#include <string>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <sstream>

#include "minui/minui.h"

#define find_prop_id(_res, type, Type, obj_id, prop_name, prop_id, index)            \
  do {                                                                               \
    int j = 0;                                                                       \
    int prop_count = 0;                                                              \
    struct Type* obj = NULL;                                                         \
    obj = (_res);                                                                    \
    if (!obj || !(obj->props) || drm[index].monitor_##type->type##_id != (obj_id)) { \
      prop_id = 0;                                                                   \
      break;                                                                         \
    }                                                                                \
    prop_count = (int)obj->props->count_props;                                       \
    for (j = 0; j < prop_count; ++j)                                                 \
      if (!strcmp(obj->props_info[j]->name, (prop_name))) break;                     \
    (prop_id) = (j == prop_count) ? 0 : obj->props_info[j]->prop_id;                 \
  } while (0)

#define add_prop(res, type, Type, id, id_name, id_val, index) \
  find_prop_id(res, type, Type, id, id_name, prop_id, index); \
  if (prop_id)                                         \
    drmModeAtomicAddProperty(atomic_req, id, prop_id, id_val);

/**
 * enum sde_rm_topology_name - HW resource use case in use by connector
 * @SDE_RM_TOPOLOGY_NONE:                 No topology in use currently
 * @SDE_RM_TOPOLOGY_SINGLEPIPE:           1 LM, 1 PP, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_SINGLEPIPE_DSC:       1 LM, 1 DSC, 1 PP, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_SINGLEPIPE_VDC:       1 LM, 1 VDC, 1 PP, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_DUALPIPE:             2 LM, 2 PP, 2 INTF/WB
 * @SDE_RM_TOPOLOGY_DUALPIPE_DSC:         2 LM, 2 DSC, 2 PP, 2 INTF/WB
 * @SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE:     2 LM, 2 PP, 3DMux, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE_DSC: 2 LM, 2 PP, 3DMux, 1 DSC, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE_VDC: 2 LM, 2 PP, 3DMux, 1 VDC, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_DUALPIPE_DSCMERGE:    2 LM, 2 PP, 2 DSC Merge, 1 INTF/WB
 * @SDE_RM_TOPOLOGY_PPSPLIT:              1 LM, 2 PPs, 2 INTF/WB
 * @SDE_RM_TOPOLOGY_QUADPIPE_3DMERGE      4 LM, 4 PP, 3DMux, 2 INTF
 * @SDE_RM_TOPOLOGY_QUADPIPE_3DMERGE_DSC  4 LM, 4 PP, 3DMux, 3 DSC, 2 INTF
 * @SDE_RM_TOPOLOGY_QUADPIPE_DSCMERE      4 LM, 4 PP, 4 DSC Merge, 2 INTF
 * @SDE_RM_TOPOLOGY_QUADPIPE_DSC4HSMERGE  4 LM, 4 PP, 4 DSC Merge, 1 INTF
 */

static uint32_t get_lm_number(const std::string &topology) {
  if (topology == "sde_singlepipe") return 1;
  if (topology == "sde_singlepipe_dsc") return 1;
  if (topology == "sde_singlepipe_vdc") return 1;
  if (topology == "sde_dualpipe") return 2;
  if (topology == "sde_dualpipe_dsc") return 2;
  if (topology == "sde_dualpipe_vdc") return 2;
  if (topology == "sde_dualpipemerge") return 2;
  if (topology == "sde_dualpipemerge_dsc") return 2;
  if (topology == "sde_dualpipemerge_vdc") return 2;
  if (topology == "sde_dualpipe_dscmerge") return 2;
  if (topology == "sde_ppsplit") return 1;
  if (topology == "sde_quadpipemerge") return 4;
  if (topology == "sde_quadpipe_3dmerge_dsc") return 4;
  if (topology == "sde_quadpipe_dscmerge") return 4;
  if (topology == "sde_quadpipe_dsc4hsmerge") return 4;
  return DEFAULT_NUM_LMS;
}

static uint32_t get_topology_lm_number(int fd, uint32_t blob_id) {
  uint32_t num_lm = DEFAULT_NUM_LMS;

  drmModePropertyBlobRes *blob = drmModeGetPropertyBlob(fd, blob_id);
  if (!blob) {
    return num_lm;
  }

  const char *fmt_str = (const char *)(blob->data);
  std::stringstream stream(fmt_str);
  std::string line = {};
  const std::string topology = "topology=";

  while (std::getline(stream, line)) {
    if (line.find(topology) != std::string::npos) {
        num_lm = get_lm_number(std::string(line, topology.length()));
        break;
    }
  }

  drmModeFreePropertyBlob(blob);
  return num_lm;
}

static bool is_primary_plane(int fd, uint32_t blob_id) {

  bool primary = false;
  uint32_t master_plane_id_val = 0;
  drmModePropertyBlobRes *blob = drmModeGetPropertyBlob(fd, blob_id);
  if (!blob)
    return false;

  const char *fmt_str = (const char *)(blob->data);
  std::stringstream stream(fmt_str);
  std::string line = {};
  const std::string master_plane = "primary_smart_plane_id=";

  while (std::getline(stream, line)) {
    if (line.find(master_plane) != std::string::npos) {
        line = std::string(line, master_plane.length());
        master_plane_id_val = std::atoi(line.c_str());
        break;
    }
  }

  drmModeFreePropertyBlob(blob);
  primary = master_plane_id_val ? false : true;
  return primary;
}

static int find_plane_prop_id(uint32_t obj_id, const char *prop_name,
                              Plane *plane_res) {
  int i, j = 0;
  int prop_count = 0;
  struct Plane *obj = NULL;

  for (i = 0; i < NUM_PLANES; ++i) {
    obj = &plane_res[i];
    if (!obj || obj->plane->plane_id != obj_id)
      continue;
    prop_count = (int)obj->props->count_props;
    for (j = 0; j < prop_count; ++j)
      if (!strcmp(obj->props_info[j]->name, prop_name))
       return obj->props_info[j]->prop_id;
    break;
  }

  return 0;
}

static int atomic_add_prop_to_plane(Plane *plane_res, drmModeAtomicReq *req,
                                    uint32_t obj_id, const char *prop_name,
                                    uint64_t value) {
  uint32_t prop_id;

  prop_id = find_plane_prop_id(obj_id, prop_name, plane_res);
  if (prop_id == 0) {
    printf("Could not find obj_id = %d\n", obj_id);
    return -EINVAL;
  }

  if (drmModeAtomicAddProperty(req, obj_id, prop_id, value) < 0) {
    printf("Could not add prop_id = %d for obj_id %d\n",
            prop_id, obj_id);
    return -EINVAL;
  }

  return 0;
}

static int SetupSprBlobV1(int fd, uint32_t* blob_id) {
  SPRPackType pack_type = SPRPackType::kPentile;
  SPRFilterType filter_type = SPRFilterType::kFourTap;
  SPRAdaptiveModeType adpative_mode = SPRAdaptiveModeType::kYYGM;

  drm_msm_spr_init_cfg spr_init_cfg;
  spr_init_cfg.cfg0 = 1;
  spr_init_cfg.cfg1 = 1;
  spr_init_cfg.cfg2 = 1;
  spr_init_cfg.cfg3 = 0;
  spr_init_cfg.flags = 0;
  spr_init_cfg.cfg4 = (pack_type == SPRPackType::kRGBW);
  spr_init_cfg.cfg5 = kDefaultColorPhaseIncrement.at(pack_type);
  spr_init_cfg.cfg6 = kDefaultColorPhaseRepeat.at(pack_type);
  spr_init_cfg.cfg7 = static_cast<uint16_t>(filter_type);
  spr_init_cfg.cfg8 = static_cast<uint16_t>(adpative_mode);
  if (pack_type == SPRPackType::kRGBW) {
    spr_init_cfg.cfg9 = 512;
    std::copy(kDefaultRGBWGains.begin(), kDefaultRGBWGains.end(), spr_init_cfg.cfg11);
  }
  spr_init_cfg.cfg10 = 0;
  std::copy(kDecimationRatioMap.at(pack_type).begin(), kDecimationRatioMap.at(pack_type).end(),
            spr_init_cfg.cfg11);
  std::copy(kDefaultOPRGains.begin(), kDefaultOPRGains.end(), spr_init_cfg.cfg13);
  std::copy(kDefaultAdaptiveStrengths.begin(), kDefaultAdaptiveStrengths.end(), spr_init_cfg.cfg14);
  std::copy(kDefaultOPROffsets.begin(), kDefaultOPROffsets.end(), spr_init_cfg.cfg15);
  std::copy(kDefaultFilterCoeffsMap.at(filter_type).begin(),
            kDefaultFilterCoeffsMap.at(filter_type).end(), spr_init_cfg.cfg16);
  std::copy(kDefaultColorPhaseMap.at(pack_type).begin(), kDefaultColorPhaseMap.at(pack_type).end(),
            spr_init_cfg.cfg17);

  if (drmModeCreatePropertyBlob(fd, &spr_init_cfg, sizeof(drm_msm_spr_init_cfg), blob_id)) {
    printf("failed to create spr blob\n");
    return -EINVAL;
  }

  return 0;
}

static int SetupSprBlobV2(int fd, uint32_t* blob_id) {
  SPRPackType pack_type = SPRPackType::kPentile;
  SPRFilterType filter_type = SPRFilterType::kFourTap;
  SPRAdaptiveModeType adpative_mode = SPRAdaptiveModeType::kYYGM;

  drm_msm_spr_init_cfg_v2 spr_init_cfg_v2;
  spr_init_cfg_v2.cfg0 = 1;
  spr_init_cfg_v2.cfg1 = 1;
  spr_init_cfg_v2.cfg2 = 1;
  spr_init_cfg_v2.cfg3 = 0;
  spr_init_cfg_v2.flags = 0;
  spr_init_cfg_v2.cfg4 = (pack_type == SPRPackType::kRGBW);
  spr_init_cfg_v2.cfg5 = kDefaultColorPhaseIncrement.at(pack_type);
  spr_init_cfg_v2.cfg6 = kDefaultColorPhaseRepeat.at(pack_type);
  spr_init_cfg_v2.cfg7 = static_cast<uint16_t>(filter_type);
  spr_init_cfg_v2.cfg8 = static_cast<uint16_t>(adpative_mode);
  if (pack_type == SPRPackType::kRGBW) {
    spr_init_cfg_v2.cfg9 = 512;
    std::copy(kDefaultRGBWGains.begin(), kDefaultRGBWGains.end(), spr_init_cfg_v2.cfg11);
  }
  spr_init_cfg_v2.cfg10 = 0;
  std::copy(kDecimationRatioMap.at(pack_type).begin(), kDecimationRatioMap.at(pack_type).end(),
            spr_init_cfg_v2.cfg11);
  std::copy(kDefaultOPRGains.begin(), kDefaultOPRGains.end(), spr_init_cfg_v2.cfg13);
  std::copy(kDefaultAdaptiveStrengths.begin(), kDefaultAdaptiveStrengths.end(),
            spr_init_cfg_v2.cfg14);
  std::copy(kDefaultOPROffsets.begin(), kDefaultOPROffsets.end(), spr_init_cfg_v2.cfg15);
  std::copy(kDefaultFilterCoeffsMap.at(filter_type).begin(),
            kDefaultFilterCoeffsMap.at(filter_type).end(), spr_init_cfg_v2.cfg16);
  std::copy(kDefaultColorPhaseMap.at(pack_type).begin(), kDefaultColorPhaseMap.at(pack_type).end(),
            spr_init_cfg_v2.cfg17);

  if (drmModeCreatePropertyBlob(fd, &spr_init_cfg_v2, sizeof(drm_msm_spr_init_cfg_v2), blob_id)) {
    printf("failed to create spr blob\n");
    return -EINVAL;
  }

  return 0;
}

static int SetupSprBlob(int fd, const std::string& prop_name, uint32_t* blob_id) {
  int ret = 0;
  if (prop_name == "SDE_SPR_INIT_CFG_V1") {
    ret = SetupSprBlobV1(fd, blob_id);
  } else if (prop_name == "SDE_SPR_INIT_CFG_V2") {
    ret = SetupSprBlobV2(fd, blob_id);
  } else {
    ret = -ENOENT;
  }

  return ret;
}

int MinuiBackendDrm::AtomicPopulatePlane(int plane, drmModeAtomicReqPtr atomic_req, DrmConnector index) {
  uint32_t src_x, src_y, src_w, src_h;
  uint32_t crtc_x, crtc_y, crtc_w, crtc_h;
  int width = drm[index].monitor_crtc->mode.hdisplay;
  int height = drm[index].monitor_crtc->mode.vdisplay;
  int zpos = 0;

  src_y = 0;
  src_w = width/number_of_lms;
  src_h =  height;
  crtc_y = 0;
  crtc_w = width/number_of_lms;
  crtc_h = height;
  src_x = (width/number_of_lms) * plane;
  crtc_x = (width/number_of_lms) * plane;

  /* populate z-order property required for 4 layer mixer */
  if (number_of_lms == 4)
    zpos = plane >> 1;

  atomic_add_prop_to_plane(plane_res, atomic_req,
                           plane_res[plane].plane->plane_id, "zpos", zpos);

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "FB_ID",
                               drm[index].GRSurfaceDrms[drm[index].current_buffer]->fb_id))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "SRC_X", src_x << 16))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "SRC_Y", src_y << 16))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "SRC_W", src_w << 16))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "SRC_H", src_h << 16))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "CRTC_X", crtc_x))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "CRTC_Y", crtc_y))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "CRTC_W", crtc_w))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "CRTC_H", crtc_h))
    return -EINVAL;

  if (atomic_add_prop_to_plane(plane_res, atomic_req,
                               plane_res[plane].plane->plane_id, "CRTC_ID",
                               drm[index].monitor_crtc->crtc_id))
    return -EINVAL;

  return 0;
}

int MinuiBackendDrm::TeardownPipeline(drmModeAtomicReqPtr atomic_req, DrmConnector index) {
  uint32_t i, prop_id;
  int ret;

  /* During suspend, tear down pipeline */
  add_prop(&conn_res, connector, Connector, drm[index].monitor_connector->connector_id, "CRTC_ID", 0, index);
  add_prop(&crtc_res, crtc, Crtc, drm[index].monitor_crtc->crtc_id, "MODE_ID", 0, index);
  add_prop(&crtc_res, crtc, Crtc, drm[index].monitor_crtc->crtc_id, "ACTIVE", 0, index);
  if (spr_enabled) {
    add_prop(&crtc_res, crtc, Crtc, drm[index].monitor_crtc->crtc_id, spr_prop_name.c_str(), 0,
             index);
  }

  for(i = 0; i < number_of_lms; i++) {
    ret = atomic_add_prop_to_plane(plane_res, atomic_req,
                                   plane_res[i].plane->plane_id, "CRTC_ID", 0);
    if (ret < 0) {
      printf("Failed to tear down plane %d\n", i);
      return ret;
    }

    if (drmModeAtomicAddProperty(atomic_req, plane_res[i].plane->plane_id, fb_prop_id, 0) < 0) {
      printf("Failed to add property for plane_id=%d\n", plane_res[i].plane->plane_id);
      return -EINVAL;
    }
  }

  return 0;
}

int MinuiBackendDrm::SetupPipeline(drmModeAtomicReqPtr atomic_req, DrmConnector index) {
  uint32_t i, prop_id;
  int ret;

  for(i = 0; i < number_of_lms; i++) {
    add_prop(&conn_res, connector, Connector, drm[index].monitor_connector->connector_id,
         "CRTC_ID", drm[index].monitor_crtc->crtc_id, index);
    add_prop(&crtc_res, crtc, Crtc, drm[index].monitor_crtc->crtc_id, "MODE_ID", crtc_res.mode_blob_id, index);
    add_prop(&crtc_res, crtc, Crtc, drm[index].monitor_crtc->crtc_id, "ACTIVE", 1, index);
    if (spr_enabled) {
      add_prop(&crtc_res, crtc, Crtc, drm[index].monitor_crtc->crtc_id, spr_prop_name.c_str(),
               crtc_res.spr_blob_id, index);
    }
  }

  /* Setup planes */
  for(i = 0; i < number_of_lms; i++) {
    ret = AtomicPopulatePlane(i, atomic_req, index);
    if (ret < 0) {
      printf("Error populating plane_id = %d\n", plane_res[i].plane->plane_id);
      return ret;
    }
  }

  return 0;
}

GRSurfaceDrm::~GRSurfaceDrm() {
  if (mmapped_buffer_) {
    munmap(mmapped_buffer_, row_bytes * height);
  }

  if (fb_id) {
    if (drmModeRmFB(drm_fd_, fb_id) != 0) {
      perror("Failed to drmModeRmFB");
      // Falling through to free other resources.
    }
  }

  if (handle) {
    drm_gem_close gem_close = {};
    gem_close.handle = handle;

    if (drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &gem_close) != 0) {
      perror("Failed to DRM_IOCTL_GEM_CLOSE");
    }
  }
}

static int drm_format_to_bpp(uint32_t format) {
  switch (format) {
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_BGRA8888:
    case DRM_FORMAT_RGBX8888:
    case DRM_FORMAT_RGBA8888:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_BGRX8888:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_XRGB8888:
      return 32;
    case DRM_FORMAT_RGB565:
      return 16;
    default:
      printf("Unknown format %d\n", format);
      return 32;
  }
}

std::unique_ptr<GRSurfaceDrm> GRSurfaceDrm::Create(int drm_fd, int width, int height) {
  uint32_t format;
  PixelFormat pixel_format = gr_pixel_format();
  // PixelFormat comes in byte order, whereas DRM_FORMAT_* uses little-endian
  // (external/libdrm/include/drm/drm_fourcc.h). Note that although drm_fourcc.h also defines a
  // macro of DRM_FORMAT_BIG_ENDIAN, it doesn't seem to be actually supported (see the discussion
  // in https://lists.freedesktop.org/archives/amd-gfx/2017-May/008560.html).
  if (pixel_format == PixelFormat::ABGR) {
    format = DRM_FORMAT_RGBA8888;
  } else if (pixel_format == PixelFormat::BGRA) {
    format = DRM_FORMAT_ARGB8888;
  } else if (pixel_format == PixelFormat::RGBX) {
    format = DRM_FORMAT_XBGR8888;
  } else if (pixel_format == PixelFormat::ARGB) {
    format = DRM_FORMAT_BGRA8888;
  } else {
    format = DRM_FORMAT_RGB565;
  }

  drm_mode_create_dumb create_dumb = {};
  create_dumb.height = height;
  create_dumb.width = width;
  create_dumb.bpp = drm_format_to_bpp(format);
  create_dumb.flags = 0;

  if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb) != 0) {
    perror("Failed to DRM_IOCTL_MODE_CREATE_DUMB");
    return nullptr;
  }
  printf("Allocating buffer with resolution %d x %d pitch: %d bpp: %d, size: %llu\n", width, height,
         create_dumb.pitch, create_dumb.bpp, create_dumb.size);

  // Cannot use std::make_unique to access non-public ctor.
  auto surface = std::unique_ptr<GRSurfaceDrm>(new GRSurfaceDrm(
      width, height, create_dumb.pitch, create_dumb.bpp / 8, drm_fd, create_dumb.handle));

  uint32_t handles[4], pitches[4], offsets[4];

  handles[0] = surface->handle;
  pitches[0] = create_dumb.pitch;
  offsets[0] = 0;
  if (drmModeAddFB2(drm_fd, width, height, format, handles, pitches, offsets, &surface->fb_id, 0) !=
      0) {
    perror("Failed to drmModeAddFB2");
    return nullptr;
  }

  drm_mode_map_dumb map_dumb = {};
  map_dumb.handle = create_dumb.handle;
  if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb) != 0) {
    perror("Failed to DRM_IOCTL_MODE_MAP_DUMB");
    return nullptr;
  }

  auto mmapped =
      mmap(nullptr, create_dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, map_dumb.offset);
  if (mmapped == MAP_FAILED) {
    perror("Failed to mmap()");
    return nullptr;
  }
  surface->mmapped_buffer_ = static_cast<uint8_t*>(mmapped);
  printf("Framebuffer of size %llu allocated @ %p\n", create_dumb.size, surface->mmapped_buffer_);
  return surface;
}

int MinuiBackendDrm::DrmDisableCrtc(drmModeAtomicReqPtr atomic_req, DrmConnector index) {
  return TeardownPipeline(atomic_req, index);
}

int MinuiBackendDrm::DrmEnableCrtc(drmModeAtomicReqPtr atomic_req, DrmConnector index){
  return SetupPipeline(atomic_req, index);
}

void MinuiBackendDrm::Blank(bool blank) {
  Blank(blank, DRM_MAIN);
}

int MinuiBackendDrm::Initdisplay(DrmConnector index) {
  /* Get possible plane_ids */
  drmModePlaneRes* plane_options = drmModeGetPlaneResources(drm_fd);
  if (!plane_options || !plane_options->planes || (plane_options->count_planes < number_of_lms))
    return -1;

  /* Set crtc resources */
  crtc_res.props =
      drmModeObjectGetProperties(drm_fd, drm[index].monitor_crtc->crtc_id, DRM_MODE_OBJECT_CRTC);
  if (!crtc_res.props) return -1;

  crtc_res.props_info = static_cast<drmModePropertyRes**>(
      calloc(crtc_res.props->count_props, sizeof(crtc_res.props_info)));
  if (!crtc_res.props_info)
    return -1;
  else
    for (int j = 0; j < (int)crtc_res.props->count_props; ++j) {
      crtc_res.props_info[j] = drmModeGetProperty(drm_fd, crtc_res.props->props[j]);
      /* Get spr property name */
      if (!strcmp(crtc_res.props_info[j]->name, "SDE_SPR_INIT_CFG_V1") ||
          !strcmp(crtc_res.props_info[j]->name, "SDE_SPR_INIT_CFG_V2")) {
        spr_prop_name = crtc_res.props_info[j]->name;
      }
    }

  /* Set connector resources */
  conn_res.props = drmModeObjectGetProperties(drm_fd, drm[index].monitor_connector->connector_id,
                                              DRM_MODE_OBJECT_CONNECTOR);
  if (!conn_res.props) return -1;

  conn_res.props_info = static_cast<drmModePropertyRes**>(
      calloc(conn_res.props->count_props, sizeof(conn_res.props_info)));
  if (!conn_res.props_info)
    return -1;
  else {
    for (int j = 0; j < (int)conn_res.props->count_props; ++j) {
      conn_res.props_info[j] = drmModeGetProperty(drm_fd, conn_res.props->props[j]);

      /* Get preferred mode information and extract the
       * number of layer mixers needed from the topology name.
       */
      if (!strcmp(conn_res.props_info[j]->name, "mode_properties")) {
        number_of_lms = get_topology_lm_number(drm_fd, conn_res.props->prop_values[j]);
        printf("number of lms in topology %d\n", number_of_lms);
      }
    }
  }

  printf("plane count:%d\n",plane_options->count_planes);
  struct Plane *plane_all_res;
  uint32_t counter = 0;
  plane_all_res = static_cast<struct Plane *>(
     calloc(plane_options->count_planes, sizeof(struct Plane)));
  /* Set plane resources */
  for (uint32_t i = 0; i < plane_options->count_planes; ++i) {
    plane_all_res[i].plane = drmModeGetPlane(drm_fd, plane_options->planes[i]);
    if (!plane_all_res[i].plane)return -1;
  }

  for (uint32_t i = plane_options->count_planes - 1; i >= 0; i--) {
    struct Plane* obj = &plane_all_res[i];
    unsigned int j;
    obj->props = drmModeObjectGetProperties(drm_fd, obj->plane->plane_id, DRM_MODE_OBJECT_PLANE);
    if (!obj->props) continue;
    obj->props_info = static_cast<drmModePropertyRes**>(
        calloc(obj->props->count_props, sizeof(*obj->props_info)));
    if (!obj->props_info) continue;
    for (j = 0; j < obj->props->count_props; ++j) {
      obj->props_info[j] = drmModeGetProperty(drm_fd, obj->props->props[j]);

      if (!strcmp(obj->props_info[j]->name, "capabilities")) {
        if(is_primary_plane(drm_fd, obj->props->prop_values[j])) {
          plane_res[counter] = plane_all_res[i];
          counter++;
      }
     }
   }
   if (counter == number_of_lms)
     break;
  }

  drmModeFreePlaneResources(plane_options);
  plane_options = NULL;

  /* Setup spr blob id if enabled */
  if (spr_enabled) {
    if (SetupSprBlob(drm_fd, spr_prop_name, &crtc_res.spr_blob_id)) {
      return -1;
    }
  }

  /* Setup pipe and blob_id */
  if (drmModeCreatePropertyBlob(drm_fd, &drm[index].monitor_crtc->mode, sizeof(drmModeModeInfo),
                                &crtc_res.mode_blob_id)) {
    printf("failed to create mode blob\n");
    return -1;
  }

  /* Save fb_prop_id*/
  uint32_t prop_id;
  prop_id = find_plane_prop_id(plane_res[0].plane->plane_id, "FB_ID", plane_res);
  fb_prop_id = prop_id;
  return 0;
}

void MinuiBackendDrm::Blank(bool blank, DrmConnector index) {
  const auto* drmInterface = &drm[DRM_MAIN];

  printf("[ENTRY]MinuiBackendDrm::Blank display %d blank %d\n", index, blank);

  switch (index) {
    case DRM_MAIN:
      drmInterface = &drm[DRM_MAIN];
      break;
    case DRM_SEC:
      drmInterface = &drm[DRM_SEC];
      break;
    default:
      fprintf(stderr, "Invalid index: %d\n", index);
      return;
  }

  if (!drmInterface->monitor_connector) {
    fprintf(stderr, "Unsupported. index = %d\n", index);
    return;
  }

  int ret = 0;

  if (blank == current_blank_state[index])
    return;

  drmModeAtomicReqPtr atomic_req = drmModeAtomicAlloc();
  if (!atomic_req) {
     printf("Atomic Alloc failed\n");
     return;
  }

  if (blank)
    ret = DrmDisableCrtc(atomic_req, index);
  else {
    if (Initdisplay(index))
      printf("Failed to init display [%d]\n", index);
    ret = DrmEnableCrtc(atomic_req, index);
    active_display = index;
  }
  printf("[MID]MinuiBackendDrm::DrmEnableCrtc/DrmDisableCrtc[%d] err [%d]\n", blank, ret);

  if (!ret)
    ret = drmModeAtomicCommit(drm_fd, atomic_req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);

  if (!ret) {
    printf("Atomic Commit succeed");
    current_blank_state[index] = blank;
  }
  else {
    printf("Atomic Commit failed, rc = %d\n", ret);
  }
  printf("[EXIT]MinuiBackendDrm::Blank display %d blank %d\n", index, blank);

  drmModeAtomicFree(atomic_req);
}

bool MinuiBackendDrm::HasMultipleConnectors() {
  return (drm[DRM_SEC].GRSurfaceDrms[0] && drm[DRM_SEC].GRSurfaceDrms[1]);
}

static drmModeCrtc* find_crtc_for_connector(int fd, drmModeRes* resources,
                                            drmModeConnector* connector) {
  // Find the encoder. If we already have one, just use it.
  drmModeEncoder* encoder;
  if (connector->encoder_id) {
    encoder = drmModeGetEncoder(fd, connector->encoder_id);
  } else {
    encoder = nullptr;
  }

  int32_t crtc;
  if (encoder && encoder->crtc_id) {
    crtc = encoder->crtc_id;
    drmModeFreeEncoder(encoder);
    return drmModeGetCrtc(fd, crtc);
  }

  // Didn't find anything, try to find a crtc and encoder combo.
  crtc = -1;
  for (int i = 0; i < connector->count_encoders; i++) {
    encoder = drmModeGetEncoder(fd, connector->encoders[i]);

    if (encoder) {
      for (int j = 0; j < resources->count_crtcs; j++) {
        if (!(encoder->possible_crtcs & (1 << j))) continue;
        crtc = resources->crtcs[j];
        break;
      }
      if (crtc >= 0) {
        drmModeFreeEncoder(encoder);
        return drmModeGetCrtc(fd, crtc);
      }
    }
  }

  return nullptr;
}

std::vector<drmModeConnector*> find_used_connector_by_type(int fd, drmModeRes* resources,
                                                           unsigned type) {
  std::vector<drmModeConnector*> drmConnectors;
  for (int i = 0; i < resources->count_connectors; i++) {
    drmModeConnector* connector = drmModeGetConnector(fd, resources->connectors[i]);
    if (connector) {
      if ((connector->connector_type == type) && (connector->connection == DRM_MODE_CONNECTED) &&
          (connector->count_modes > 0)) {
        drmConnectors.push_back(connector);
      } else {
        drmModeFreeConnector(connector);
      }
    }
  }
  return drmConnectors;
}

static drmModeConnector* find_first_connected_connector(int fd, drmModeRes* resources) {
  for (int i = 0; i < resources->count_connectors; i++) {
    drmModeConnector* connector;

    connector = drmModeGetConnector(fd, resources->connectors[i]);
    if (connector) {
      if ((connector->count_modes > 0) && (connector->connection == DRM_MODE_CONNECTED))
        return connector;

      drmModeFreeConnector(connector);
    }
  }
  return nullptr;
}

bool MinuiBackendDrm::FindAndSetMonitor(int fd, drmModeRes* resources) {
  /* Look for LVDS/eDP/DSI connectors. Those are the main screens. */
  static constexpr unsigned kConnectorPriority[] = {
    DRM_MODE_CONNECTOR_LVDS,
    DRM_MODE_CONNECTOR_eDP,
    DRM_MODE_CONNECTOR_DSI,
  };

  std::vector<drmModeConnector*> drmConnectors;
  for (int i = 0; i < arraysize(kConnectorPriority) && drmConnectors.size() < DRM_MAX; i++) {
    auto connectors = find_used_connector_by_type(fd, resources, kConnectorPriority[i]);
    for (auto connector : connectors) {
      drmConnectors.push_back(connector);
      if (drmConnectors.size() >= DRM_MAX) break;
    }
  }

  /* If we didn't find a connector, grab the first one that is connected. */
  if (drmConnectors.empty()) {
    drmModeConnector* connector = find_first_connected_connector(fd, resources);
    if (connector) {
      drmConnectors.push_back(connector);
    }
  }

  for (int drm_index = 0; drm_index < drmConnectors.size(); drm_index++) {
    drm[drm_index].monitor_connector = drmConnectors[drm_index];

    drm[drm_index].selected_mode = 0;
    for (int modes = 0; modes < drmConnectors[drm_index]->count_modes; modes++) {
      printf("Display Mode %d resolution: %d x %d @ %d FPS\n", modes,
             drmConnectors[drm_index]->modes[modes].hdisplay,
             drmConnectors[drm_index]->modes[modes].vdisplay,
             drmConnectors[drm_index]->modes[modes].vrefresh);
      if (drmConnectors[drm_index]->modes[modes].type & DRM_MODE_TYPE_PREFERRED) {
        printf("Choosing display mode #%d\n", modes);
        drm[drm_index].selected_mode = modes;
        break;
      }
    }
  }

  return drmConnectors.size() > 0;
}

void MinuiBackendDrm::DisableNonMainCrtcs(int fd, drmModeRes* resources, drmModeCrtc* main_crtc) {
  uint32_t prop_id;
  int ret = 0;
  drmModeAtomicReqPtr atomic_req = drmModeAtomicAlloc();

  for (int i = 0; i < resources->count_connectors; i++) {
    drmModeConnector* connector = drmModeGetConnector(fd, resources->connectors[i]);
    drmModeCrtc* crtc = find_crtc_for_connector(fd, resources, connector);
    if (crtc->crtc_id != main_crtc->crtc_id) {
      // Switching to atomic commit. Given only crtc, we can only set ACTIVE = 0
      // to disable any Nonmain CRTCs
      find_prop_id(&crtc_res, crtc, Crtc, crtc->crtc_id, "ACTIVE", prop_id, DRM_SEC);
      if (prop_id == 0)
        return;

      if (drmModeAtomicAddProperty(atomic_req, drm[DRM_SEC].monitor_crtc->crtc_id, prop_id, 0) < 0)
        return;
    }
    drmModeFreeCrtc(crtc);
  }

  ret = drmModeAtomicCommit(drm_fd, atomic_req,DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
  if (ret)
    printf("Atomic Commit failed in DisableNonMainCrtcs\n");

  drmModeAtomicFree(atomic_req);
}

void MinuiBackendDrm::UpdatePlaneFB(DrmConnector index) {
  uint32_t i, prop_id;

  /* Set atomic req */
  drmModeAtomicReqPtr atomic_req = drmModeAtomicAlloc();
  if (!atomic_req) {
     printf("Atomic Alloc failed. Could not update fb_id\n");
     return;
  }

  /* Add conn-crtc association property required
   * for driver to recognize quadpipe topology.
   */
  add_prop(&conn_res, connector, Connector, drm[index].monitor_connector->connector_id,
           "CRTC_ID", drm[index].monitor_crtc->crtc_id, index);

  /* Add property */
  for(i = 0; i < number_of_lms; i++)
    drmModeAtomicAddProperty(atomic_req, plane_res[i].plane->plane_id,
                             fb_prop_id, drm[index].GRSurfaceDrms[drm[index].current_buffer]->fb_id);

  /* Commit changes */
  int32_t ret;
  ret = drmModeAtomicCommit(drm_fd, atomic_req,
                 DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);

  drmModeAtomicFree(atomic_req);

  if (ret)
    printf("Atomic commit failed ret=%d\n", ret);
}

GRSurface* MinuiBackendDrm::Init() {
  int ret = 0;
  drmModeRes* res = nullptr;
  drm_fd = -1;

  spr_enabled = android::base::GetIntProperty("vendor.display.enable_spr", 0);
  number_of_lms = DEFAULT_NUM_LMS;
  /* Consider DRM devices in order. */
  for (int i = 0; i < DRM_MAX_MINOR; i++) {
    auto dev_name = android::base::StringPrintf(DRM_DEV_NAME, DRM_DIR_NAME, i);
    android::base::unique_fd fd(open(dev_name.c_str(), O_RDWR | O_CLOEXEC));
    if (fd == -1) continue;

    /* We need dumb buffers. */
    if (uint64_t cap = 0; drmGetCap(fd.get(), DRM_CAP_DUMB_BUFFER, &cap) != 0 || cap == 0) {
      continue;
    }

    res = drmModeGetResources(fd.get());
    if (!res) {
      continue;
    }

    /* Use this device if it has at least one connected monitor. */
    if (res->count_crtcs > 0 && res->count_connectors > 0) {
      if (find_first_connected_connector(fd.get(), res)) {
        drm_fd = fd.release();
        break;
      }
    }

    drmModeFreeResources(res);
    res = nullptr;
  }

  if (drm_fd == -1 || res == nullptr) {
    perror("Failed to find/open a drm device");
    return nullptr;
  }

  if (!FindAndSetMonitor(drm_fd, res)) {
    fprintf(stderr, "Failed to find main monitor_connector\n");
    drmModeFreeResources(res);
    return nullptr;
  }

  for (int i = 0; i < DRM_MAX; i++) {
    if (drm[i].monitor_connector) {
      drm[i].monitor_crtc = find_crtc_for_connector(drm_fd, res, drm[i].monitor_connector);
      if (!drm[i].monitor_crtc) {
        fprintf(stderr, "Failed to find monitor_crtc, drm index=%d\n", i);
        drmModeFreeResources(res);
        return nullptr;
      }

      drm[i].monitor_crtc->mode = drm[i].monitor_connector->modes[drm[i].selected_mode];

      int width = drm[i].monitor_crtc->mode.hdisplay;
      int height = drm[i].monitor_crtc->mode.vdisplay;

      drm[i].GRSurfaceDrms[0] = GRSurfaceDrm::Create(drm_fd, width, height);
      drm[i].GRSurfaceDrms[1] = GRSurfaceDrm::Create(drm_fd, width, height);
      if (!drm[i].GRSurfaceDrms[0] || !drm[i].GRSurfaceDrms[1]) {
        fprintf(stderr, "Failed to create GRSurfaceDrm, drm index=%d\n", i);
        drmModeFreeResources(res);
        return nullptr;
      }

      drm[i].current_buffer = 0;
    }
  }

  DisableNonMainCrtcs(drm_fd, res, drm[DRM_MAIN].monitor_crtc);

  drmModeFreeResources(res);

  drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
  drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);

  ret = Initdisplay(DRM_MAIN);
  if (ret) {
    printf("Failed to init display\n");
    return NULL;
  }

  current_blank_state[DRM_MAIN] = true;
  current_blank_state[DRM_SEC] = true;

  Blank(false);

  return drm[DRM_MAIN].GRSurfaceDrms[0].get();
}

GRSurface* MinuiBackendDrm::Flip() {
  UpdatePlaneFB(active_display);

  drm[active_display].current_buffer = 1 - drm[active_display].current_buffer;
  return drm[active_display].GRSurfaceDrms[drm[active_display].current_buffer].get();
}

MinuiBackendDrm::~MinuiBackendDrm() {
  for (int i = 0; i < DRM_MAX; i++) {
    if (drm[i].monitor_connector) {
      drmModeFreeCrtc(drm[i].monitor_crtc);
      drmModeFreeConnector(drm[i].monitor_connector);
    }
  }
  Blank(true);
  drmModeDestroyPropertyBlob(drm_fd, crtc_res.mode_blob_id);
  close(drm_fd);
  drm_fd = -1;
}
