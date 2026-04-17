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
      'shaders/ohos_triangle/glsl/triangle.vert.spv',
      'shaders/ohos_triangle/glsl/triangle.frag.spv',
    ];
    for (let file of files) {
      let buffer = getContext(this).resourceManager.getRawFileContentSync(file);
      let dest = filesDir + '/' + file.split('/').pop();  // flatten to filename only
      let fd = fs.openSync(dest, fs.OpenMode.READ_WRITE | fs.OpenMode.CREATE);
      fs.writeSync(fd.fd, buffer.buffer);
      fs.closeSync(fd);
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
