import fs from '@ohos.file.fs';

@Entry
@Component
struct Index {
  @State message: string = 'Vulkan OHOS Demo';

  aboutToAppear(): void {
    // Extract shader .spv files from rawfile to sandbox files directory.
    // Rawfile resources can only be accessed via resourceManager API (not filesystem paths).
    // Write them to the writable sandbox so native code can read via std::ifstream.
    let filesDir = getContext(this).filesDir;

    // Read rawfile content and write to sandbox
    let files: string[] = [
      'shaders/fluid_particles/glsl/particle_init.comp.spv',
      'shaders/fluid_particles/glsl/particle_update.comp.spv',
      'shaders/fluid_particles/glsl/particle.vert.spv',
      'shaders/fluid_particles/glsl/particle.frag.spv',
      'shaders/fluid_particles/glsl/fluid_inject.comp.spv',
      'shaders/fluid_particles/glsl/fluid_advect.comp.spv',
      'shaders/fluid_particles/glsl/fluid_boundary.comp.spv',
      'shaders/fluid_particles/glsl/fluid_divergence.comp.spv',
      'shaders/fluid_particles/glsl/fluid_boundary_scalar.comp.spv',
      'shaders/fluid_particles/glsl/fluid_pressure.comp.spv',
      'shaders/fluid_particles/glsl/fluid_gradient_subtract.comp.spv',
      'shaders/fluid_particles/glsl/fluid_clear.comp.spv',
      'shaders/fluid_particles/glsl/fluid_vorticity.comp.spv',
      'shaders/fluid_particles/glsl/fluid_vorticity_confinement.comp.spv',
      'shaders/ohos_triangle/glsl/postprocessing.vert.spv',
      'shaders/ohos_triangle/glsl/brightpass.frag.spv',
      'shaders/ohos_triangle/glsl/blur_h.frag.spv',
      'shaders/ohos_triangle/glsl/blur_v.frag.spv',
      'shaders/ohos_triangle/glsl/composite.frag.spv',
      'shaders/imgui.vert.spv',
      'shaders/imgui.frag.spv',
      'assets/fonts/Roboto-Regular.ttf',
      'assets/fonts/RobotoMono-Regular.ttf',
    ];
    for (let file of files) {
      try {
        let buffer = getContext(this).resourceManager.getRawFileContentSync(file);
        let dest = filesDir + '/' + file;
        // Create parent directories to preserve subdirectory structure
        let dir = dest.substring(0, dest.lastIndexOf('/'));
        if (!fs.accessSync(dir)) {
          fs.mkdirSync(dir, true);
        }
        let fd = fs.openSync(dest, fs.OpenMode.READ_WRITE | fs.OpenMode.CREATE);
        fs.writeSync(fd.fd, buffer.buffer);
        fs.closeSync(fd);
      } catch (e) {
        console.error(`Failed to extract shader: ${file}, error: ${e}`);
      }
    }
  }

  build() {
    Row() {
      Column() {
        XComponent({
          id: 'vulkan_xc',
          type: 'surface',
          libraryname: 'entry'
        })
        .width('100%')
        .height('100%')
      }
      .width('100%')
    }
    .height('100%')
  }
}
