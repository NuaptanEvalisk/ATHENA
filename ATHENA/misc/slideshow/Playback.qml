pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D

Item {
    id: root
    property real slideWidth: 1
    property real slideHeight: 1
    property var layers: []
    property var layerStates: []
    property string previousSource: ""
    property string transition: "none"
    property string direction: "left"
    property real progress: 1
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    property bool obscured: false
    property bool pointerVisible: false
    property point pointerPosition: Qt.point(0, 0)
    readonly property bool changing: progress < 1 && previousSource !== "" && transition !== "none"
    readonly property bool cube: changing && transition === "cube"
    readonly property bool vertical: direction === "up" || direction === "down"
    readonly property real sign: direction === "right" || direction === "down" ? 1 : -1
    readonly property real fit: Math.min(width / slideWidth, height / slideHeight) * zoom

    Item {
        id: stage
        width: root.slideWidth
        height: root.slideHeight
        x: (root.width - width) / 2 + root.panX
        y: (root.height - height) / 2 + root.panY
        scale: root.fit
        visible: !root.obscured
        clip: true

        Image {
            id: previous
            width: stage.width
            height: stage.height
            source: root.previousSource
            cache: false
            smooth: true
            visible: root.changing && !root.cube
            x: root.transition === "push" && !root.vertical ? root.sign * width * root.progress : 0
            y: root.transition === "push" && root.vertical ? root.sign * height * root.progress : 0
        }

        Item {
            id: current
            width: stage.width
            height: stage.height
            visible: !root.cube
            opacity: root.changing && root.transition === "fade" ? root.progress : 1
            x: root.changing && root.transition === "push" && !root.vertical ? -root.sign * width * (1-root.progress) : 0
            y: root.changing && root.transition === "push" && root.vertical ? -root.sign * height * (1-root.progress) : 0
            Repeater {
                model: root.layers
                delegate: Item {
                    id: layerItem
                    required property int index
                    required property var modelData
                    width: stage.width
                    height: stage.height
                    opacity: root.layerStates[index] ? root.layerStates[index].opacity : 1
                    transform: Matrix4x4 {
                        matrix: root.layerStates[layerItem.index] ? root.layerStates[layerItem.index].matrix : Qt.matrix4x4()
                    }
                    Image {
                        x: layerItem.modelData.x; y: layerItem.modelData.y
                        width: layerItem.modelData.width; height: layerItem.modelData.height
                        source: layerItem.modelData.source
                        smooth: true
                        cache: false
                    }
                }
            }
        }

        View3D {
            id: cubeView
            anchors.fill: parent
            visible: root.cube
            environment: SceneEnvironment {
                backgroundMode: SceneEnvironment.Transparent
                antialiasingMode: SceneEnvironment.MSAA
                antialiasingQuality: SceneEnvironment.High
            }
            readonly property real depth: root.vertical ? stage.height : stage.width
            readonly property real cameraDistance: {
                const angle = root.sign * root.progress * Math.PI / 2
                const c = Math.cos(angle), s = Math.sin(angle)
                const tangent = Math.tan(Math.PI / 8)
                const aspect = stage.width / stage.height
                let distance = 0
                for (const x of [-stage.width / 2, stage.width / 2])
                    for (const y of [-stage.height / 2, stage.height / 2])
                        for (const z of [-depth / 2, depth / 2]) {
                            const rx = root.vertical ? x : c * x + s * z
                            const ry = root.vertical ? c * y - s * z : y
                            const rz = root.vertical ? s * y + c * z : -s * x + c * z
                            distance = Math.max(distance, rz + Math.abs(ry) / tangent,
                                                rz + Math.abs(rx) / (tangent * aspect))
                        }
                return distance
            }
            camera: PerspectiveCamera {
                z: cubeView.cameraDistance
                fieldOfView: 45
                clipNear: 1
                clipFar: 100000
            }
            Node {
                eulerRotation.y: root.vertical ? 0 : root.sign * 90 * root.progress
                eulerRotation.x: root.vertical ? root.sign * 90 * root.progress : 0
                Model {
                    source: "#Rectangle"
                    z: (root.vertical ? stage.height : stage.width) / 2
                    scale: Qt.vector3d(stage.width / 100, stage.height / 100, 1)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        cullMode: Material.BackFaceCulling
                        baseColorMap: Texture { sourceItem: previous }
                    }
                }
                Model {
                    source: "#Rectangle"
                    x: root.vertical ? 0 : -root.sign * stage.width / 2
                    y: root.vertical ? root.sign * stage.height / 2 : 0
                    eulerRotation.y: root.vertical ? 0 : -root.sign * 90
                    eulerRotation.x: root.vertical ? -root.sign * 90 : 0
                    scale: Qt.vector3d(stage.width / 100, stage.height / 100, 1)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        cullMode: Material.BackFaceCulling
                        baseColorMap: Texture { sourceItem: current }
                    }
                }
            }
        }
        Rectangle {
            visible: root.pointerVisible
            x: root.pointerPosition.x * stage.width - width / 2
            y: root.pointerPosition.y * stage.height - height / 2
            width: 14 / root.fit; height: width; radius: width / 2
            color: "#ff232d"
            border.color: "white"
            border.width: 1.5 / root.fit
        }
    }
}
