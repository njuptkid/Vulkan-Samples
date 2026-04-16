@Entry
@Component
struct Index {
  @State message: string = 'Vulkan OHOS Demo';

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
