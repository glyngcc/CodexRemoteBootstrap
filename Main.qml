import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 1320
    height: 850
    minimumWidth: 1100
    minimumHeight: 720
    visible: true
    title: "Codex Remote Manager"
    color: "#F6F7FB"

    property int pageIndex: 0
    property bool useSshConfigHost: false
    property string statusText: "等待配置"
    property int progressValue: 0
    property bool successState: false
    property bool errorState: false
    property string packageStatusText: "安装包管理就绪"
    property int packageProgress: 0

    function appendLog(line) {
        if (!line || line.length === 0)
            return
        const ts = Qt.formatTime(new Date(), "HH:mm:ss")
        logArea.append("[" + ts + "] " + line)
        logArea.cursorPosition = logArea.length
    }

    function rebuildVersionModel() {
        const keep = packageVersionCombo.currentValue || "latest-stable"
        versionModel.clear()
        versionModel.append({"label": "Latest stable · 官方稳定版", "value": "latest-stable"})
        versionModel.append({"label": "Latest published · 含 Preview", "value": "latest-any"})
        const list = bootstrapController.releaseCatalog
        for (let i = 0; i < list.length; ++i) {
            const item = list[i]
            versionModel.append({
                "label": item.tag + (item.prerelease ? "   · Preview" : "   · Stable"),
                "value": item.tag
            })
        }
        let found = 0
        for (let j = 0; j < versionModel.count; ++j) {
            if (versionModel.get(j).value === keep) {
                found = j
                break
            }
        }
        packageVersionCombo.currentIndex = found
    }

    function applySshSelection() {
        const item = bootstrapController.sshHostAt(deviceCombo.currentIndex)
        if (!item || item.manual) {
            root.useSshConfigHost = false
            hostField.readOnly = false
            userField.readOnly = false
            portField.readOnly = false
            aliasField.readOnly = false
            if (deviceCombo.currentIndex === 0 && hostField.text.length === 0)
                identityField.text = bootstrapController.defaultIdentityPath
            return
        }

        root.useSshConfigHost = true
        aliasField.text = item.alias || ""
        hostField.text = item.hostName || item.alias || ""
        userField.text = item.user || ""
        portField.text = String(item.port || 22)
        identityField.text = item.identityFile || bootstrapController.defaultIdentityPath
        hostField.readOnly = true
        userField.readOnly = true
        portField.readOnly = true
        aliasField.readOnly = true
        proxySwitch.checked = false
    }

    function options() {
        return {
            "host": hostField.text.trim(),
            "port": parseInt(portField.text || "22"),
            "user": userField.text.trim(),
            "password": passwordField.text,
            "alias": aliasField.text.trim(),
            "identityPath": identityField.text.trim(),
            "packageDir": bootstrapController.defaultPackageDir,
            "packageVersion": packageVersionCombo.currentValue || "latest-stable",
            "autoDownloadPackage": autoDownloadSwitch.checked,
            "installCodex": installCodexSwitch.checked,
            "syncAuthState": syncAuthSwitch.checked,
            "proxyEnabled": proxySwitch.checked,
            "proxyHost": proxyHostField.text.trim(),
            "proxyPort": parseInt(proxyPortField.text || "7890"),
            "remoteProxyPort": parseInt(remoteProxyPortField.text || "17890"),
            "useSshConfigHost": root.useSshConfigHost,
            "manageSshConfig": !root.useSshConfigHost
        }
    }

    Connections {
        target: bootstrapController
        function onLogLine(line) { root.appendLog(line) }
        function onStepChanged(step, percent) {
            root.statusText = step
            root.progressValue = percent
            root.errorState = false
        }
        function onCompleted(ok, message) {
            root.successState = ok
            root.errorState = !ok
            root.statusText = message
            root.progressValue = ok ? 100 : root.progressValue
            root.appendLog((ok ? "[DONE] " : "[ERROR] ") + message)
        }
        function onSshHostsChanged() {
            const previous = aliasField.text
            let index = 0
            const hosts = bootstrapController.sshHosts
            for (let i = 0; i < hosts.length; ++i) {
                if (hosts[i].alias === previous) {
                    index = i
                    break
                }
            }
            deviceCombo.currentIndex = index
            root.applySshSelection()
        }
        function onReleaseCatalogChanged() { root.rebuildVersionModel() }
        function onPackageStatus(message, percent) {
            root.packageStatusText = message
            root.packageProgress = percent
        }
        function onPackageCompleted(ok, message) {
            root.packageStatusText = message
            root.packageProgress = ok ? 100 : root.packageProgress
            root.appendLog((ok ? "[PKG] " : "[PKG-ERROR] ") + message)
        }
    }

    ListModel { id: versionModel }

    FileDialog {
        id: identityFileDialog
        title: "选择 SSH 私钥"
        fileMode: FileDialog.OpenFile
        onAccepted: identityField.text = bootstrapController.localPathFromUrl(selectedFile)
    }

    component Card: Rectangle {
        radius: 16
        color: "#FFFFFF"
        border.color: "#E8EAF0"
        border.width: 1
    }

    component SectionTitle: Label {
        font.pixelSize: 15
        font.weight: Font.DemiBold
        color: "#141622"
    }

    component MutedLabel: Label {
        font.pixelSize: 12
        color: "#7A8190"
    }

    component NavButton: Button {
        property bool active: false
        Layout.fillWidth: true
        Layout.preferredHeight: 44
        flat: true
        font.pixelSize: 13
        font.weight: active ? Font.DemiBold : Font.Medium
        leftPadding: 16
        contentItem: Label {
            text: parent.text
            color: parent.active ? "#FFFFFF" : "#9FA6B7"
            font: parent.font
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 10
            color: parent.active ? "#25293A" : (parent.hovered ? "#1D2130" : "transparent")
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 224
            Layout.fillHeight: true
            color: "#11131C"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 20
                    spacing: 11
                    Rectangle {
                        width: 38; height: 38; radius: 11
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#7C5CFF" }
                            GradientStop { position: 1; color: "#5A38F0" }
                        }
                        Label {
                            anchors.centerIn: parent
                            text: ">_"
                            color: "white"
                            font.pixelSize: 15
                            font.bold: true
                        }
                    }
                    ColumnLayout {
                        spacing: 0
                        Label { text: "Codex Remote"; color: "#FFFFFF"; font.pixelSize: 15; font.bold: true }
                        Label { text: "Manager"; color: "#71798C"; font.pixelSize: 11 }
                    }
                }

                NavButton {
                    text: "⌁   远端配置"
                    active: root.pageIndex === 0
                    onClicked: root.pageIndex = 0
                }
                NavButton {
                    text: "⇩   安装包管理"
                    active: root.pageIndex === 1
                    onClicked: root.pageIndex = 1
                }
                NavButton {
                    text: "≡   运行日志"
                    active: root.pageIndex === 2
                    onClicked: root.pageIndex = 2
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 94
                    radius: 12
                    color: "#181B27"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 5
                        Label { text: "SSH Config"; color: "#DDE1EA"; font.pixelSize: 11; font.bold: true }
                        Label {
                            Layout.fillWidth: true
                            text: bootstrapController.sshConfigPath
                            color: "#727B90"
                            font.pixelSize: 10
                            wrapMode: Text.WrapAnywhere
                        }
                        Button {
                            text: "刷新设备"
                            flat: true
                            font.pixelSize: 11
                            onClicked: bootstrapController.refreshSshHosts()
                        }
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: "v0.3 · Qt 6.8"
                    color: "#555C6D"
                    font.pixelSize: 10
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#F6F7FB"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 72
                    color: "#FFFFFF"
                    border.color: "#ECEEF3"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 28
                        anchors.rightMargin: 28
                        spacing: 12
                        ColumnLayout {
                            spacing: 1
                            Label {
                                text: root.pageIndex === 0 ? "远端配置" : (root.pageIndex === 1 ? "Codex 安装包管理" : "运行日志")
                                color: "#151722"
                                font.pixelSize: 19
                                font.weight: Font.DemiBold
                            }
                            Label {
                                text: root.pageIndex === 0 ? "从 ~/.ssh/config 选择设备，或新增一台 Linux 远端" :
                                      (root.pageIndex === 1 ? "在线拉取 OpenAI Codex Release，并维护本地离线缓存" : "查看 SSH、安装、下载与验证过程")
                                color: "#8A909F"
                                font.pixelSize: 11
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            implicitWidth: stateText.implicitWidth + 22
                            implicitHeight: 30
                            radius: 15
                            color: root.errorState ? "#FFF0F0" : (root.successState ? "#EBFBF3" : "#F0F1F5")
                            Label {
                                id: stateText
                                anchors.centerIn: parent
                                text: bootstrapController.running ? "正在执行" : (root.successState ? "配置完成" : (root.errorState ? "执行失败" : "Ready"))
                                color: root.errorState ? "#D84A4A" : (root.successState ? "#198754" : "#687080")
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: root.pageIndex

                    // Page 0: Remote setup
                    ScrollView {
                        clip: true
                        contentWidth: availableWidth
                        Item {
                            width: parent.width
                            implicitHeight: setupColumn.implicitHeight + 56

                            ColumnLayout {
                                id: setupColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 28
                                spacing: 18

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 18

                                    Card {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 650
                                        implicitHeight: deviceForm.implicitHeight + 40
                                        ColumnLayout {
                                            id: deviceForm
                                            anchors.fill: parent
                                            anchors.margins: 20
                                            spacing: 14

                                            RowLayout {
                                                Layout.fillWidth: true
                                                SectionTitle { text: "连接目标" }
                                                Item { Layout.fillWidth: true }
                                                Rectangle {
                                                    radius: 8
                                                    implicitWidth: sourceLabel.implicitWidth + 16
                                                    implicitHeight: 25
                                                    color: root.useSshConfigHost ? "#EEF0FF" : "#F1F2F5"
                                                    Label {
                                                        id: sourceLabel
                                                        anchors.centerIn: parent
                                                        text: root.useSshConfigHost ? "SSH Config" : "Manual"
                                                        color: root.useSshConfigHost ? "#6246E5" : "#747B89"
                                                        font.pixelSize: 10
                                                        font.bold: true
                                                    }
                                                }
                                            }

                                            MutedLabel { text: "优先从本机 OpenSSH 配置中选择已有设备，支持 User / Port / IdentityFile / ProxyJump 等生效配置。"; wrapMode: Text.Wrap }

                                            Label { text: "设备"; color: "#515866"; font.pixelSize: 11; font.bold: true }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                ComboBox {
                                                    id: deviceCombo
                                                    Layout.fillWidth: true
                                                    model: bootstrapController.sshHosts
                                                    textRole: "label"
                                                    onActivated: root.applySshSelection()
                                                }
                                                Button {
                                                    text: "刷新"
                                                    onClicked: bootstrapController.refreshSshHosts()
                                                }
                                            }

                                            GridLayout {
                                                Layout.fillWidth: true
                                                columns: 4
                                                columnSpacing: 10
                                                rowSpacing: 10

                                                Label { text: "Host / IP"; color: "#5B6270"; font.pixelSize: 11 }
                                                TextField {
                                                    id: hostField
                                                    Layout.columnSpan: 2
                                                    Layout.fillWidth: true
                                                    placeholderText: "192.168.144.23"
                                                    onEditingFinished: {
                                                        if (!root.useSshConfigHost && aliasField.text.trim().length === 0 && text.trim().length > 0)
                                                            aliasField.text = bootstrapController.suggestedAlias(text)
                                                    }
                                                }
                                                TextField {
                                                    id: portField
                                                    Layout.preferredWidth: 90
                                                    text: "22"
                                                    placeholderText: "Port"
                                                    inputMethodHints: Qt.ImhDigitsOnly
                                                }

                                                Label { text: "User"; color: "#5B6270"; font.pixelSize: 11 }
                                                TextField {
                                                    id: userField
                                                    Layout.columnSpan: 3
                                                    Layout.fillWidth: true
                                                    placeholderText: "jetson / root / glyn"
                                                }

                                                Label { text: "SSH Alias"; color: "#5B6270"; font.pixelSize: 11 }
                                                TextField {
                                                    id: aliasField
                                                    Layout.columnSpan: 3
                                                    Layout.fillWidth: true
                                                    placeholderText: "codex-jetson-23"
                                                }

                                                Label { text: "Password"; color: "#5B6270"; font.pixelSize: 11 }
                                                TextField {
                                                    id: passwordField
                                                    Layout.columnSpan: 3
                                                    Layout.fillWidth: true
                                                    echoMode: TextInput.Password
                                                    passwordCharacter: "●"
                                                    placeholderText: root.useSshConfigHost ? "已有免密可留空；否则填首次 SSH 密码" : "仅首次配置公钥 / sudo 使用，不保存"
                                                }
                                            }

                                            Rectangle { Layout.fillWidth: true; height: 1; color: "#ECEEF3" }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 4
                                                    Label { text: "Identity file"; color: "#515866"; font.pixelSize: 11; font.bold: true }
                                                    TextField {
                                                        id: identityField
                                                        Layout.fillWidth: true
                                                        text: bootstrapController.defaultIdentityPath
                                                        readOnly: root.useSshConfigHost
                                                    }
                                                }
                                                Button {
                                                    text: "选择"
                                                    enabled: !root.useSshConfigHost
                                                    Layout.alignment: Qt.AlignBottom
                                                    onClicked: identityFileDialog.open()
                                                }
                                            }
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.preferredWidth: 360
                                        Layout.fillWidth: true
                                        spacing: 18

                                        Card {
                                            Layout.fillWidth: true
                                            implicitHeight: packageOptions.implicitHeight + 36
                                            ColumnLayout {
                                                id: packageOptions
                                                anchors.fill: parent
                                                anchors.margins: 18
                                                spacing: 12
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    SectionTitle { text: "Codex CLI" }
                                                    Item { Layout.fillWidth: true }
                                                    Switch { id: installCodexSwitch; checked: true }
                                                }
                                                Label { text: "目标版本"; color: "#5B6270"; font.pixelSize: 11 }
                                                ComboBox {
                                                    id: packageVersionCombo
                                                    Layout.fillWidth: true
                                                    model: versionModel
                                                    textRole: "label"
                                                    valueRole: "value"
                                                }
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    ColumnLayout {
                                                        spacing: 1
                                                        Label { text: "缺失时自动下载"; color: "#343946"; font.pixelSize: 12; font.bold: true }
                                                        MutedLabel { text: "Windows 在线下载，远端仍可完全离线" }
                                                    }
                                                    Item { Layout.fillWidth: true }
                                                    Switch { id: autoDownloadSwitch; checked: true }
                                                }
                                                Button {
                                                    Layout.fillWidth: true
                                                    text: "管理本地安装包"
                                                    onClicked: root.pageIndex = 1
                                                }
                                            }
                                        }

                                        Card {
                                            Layout.fillWidth: true
                                            implicitHeight: advancedOptions.implicitHeight + 36
                                            ColumnLayout {
                                                id: advancedOptions
                                                anchors.fill: parent
                                                anchors.margins: 18
                                                spacing: 10
                                                SectionTitle { text: "高级选项" }
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    Label { text: "同步本机 Codex 登录态"; color: "#343946"; font.pixelSize: 12 }
                                                    Item { Layout.fillWidth: true }
                                                    Switch { id: syncAuthSwitch }
                                                }
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    Label { text: "Windows 代理反向转发"; color: root.useSshConfigHost ? "#A8ADBA" : "#343946"; font.pixelSize: 12 }
                                                    Item { Layout.fillWidth: true }
                                                    Switch {
                                                        id: proxySwitch
                                                        enabled: !root.useSshConfigHost
                                                    }
                                                }
                                                GridLayout {
                                                    Layout.fillWidth: true
                                                    columns: 2
                                                    enabled: proxySwitch.checked
                                                    opacity: enabled ? 1 : 0.4
                                                    Label { text: "本机代理"; color: "#707786"; font.pixelSize: 10 }
                                                    TextField { id: proxyHostField; Layout.fillWidth: true; text: "127.0.0.1" }
                                                    Label { text: "端口"; color: "#707786"; font.pixelSize: 10 }
                                                    RowLayout {
                                                        TextField { id: proxyPortField; Layout.fillWidth: true; text: "7890"; inputMethodHints: Qt.ImhDigitsOnly }
                                                        Label { text: "→"; color: "#9AA0AD" }
                                                        TextField { id: remoteProxyPortField; Layout.fillWidth: true; text: "17890"; inputMethodHints: Qt.ImhDigitsOnly }
                                                    }
                                                }
                                                MutedLabel {
                                                    Layout.fillWidth: true
                                                    visible: root.useSshConfigHost
                                                    text: "现有 Host 默认不改写。需要 RemoteForward 时，请切换为手动新增一个 Codex 托管别名。"
                                                    wrapMode: Text.Wrap
                                                }
                                            }
                                        }
                                    }
                                }

                                Card {
                                    Layout.fillWidth: true
                                    implicitHeight: 142
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 20
                                        spacing: 22

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            RowLayout {
                                                Layout.fillWidth: true
                                                SectionTitle { text: "执行状态" }
                                                Item { Layout.fillWidth: true }
                                                Label { text: root.progressValue + "%"; color: "#684CF4"; font.bold: true; font.pixelSize: 12 }
                                            }
                                            ProgressBar { Layout.fillWidth: true; from: 0; to: 100; value: root.progressValue }
                                            Label {
                                                Layout.fillWidth: true
                                                text: root.statusText
                                                elide: Text.ElideRight
                                                color: root.errorState ? "#D84A4A" : (root.successState ? "#178653" : "#747B89")
                                                font.pixelSize: 11
                                            }
                                        }

                                        Button {
                                            text: "测试 SSH"
                                            Layout.preferredWidth: 125
                                            enabled: !bootstrapController.running
                                            onClicked: {
                                                root.errorState = false
                                                root.successState = false
                                                root.progressValue = 0
                                                root.appendLog("开始测试 SSH…")
                                                bootstrapController.testConnection(root.options())
                                            }
                                        }
                                        Button {
                                            text: bootstrapController.running ? "正在配置…" : "一键配置 Codex"
                                            highlighted: true
                                            Layout.preferredWidth: 180
                                            enabled: !bootstrapController.running
                                            onClicked: {
                                                root.errorState = false
                                                root.successState = false
                                                root.progressValue = 0
                                                root.appendLog("开始配置 Codex SSH 远端环境…")
                                                bootstrapController.configure(root.options())
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Page 1: Package manager
                    ScrollView {
                        clip: true
                        contentWidth: availableWidth
                        Item {
                            width: parent.width
                            implicitHeight: packagePage.implicitHeight + 56
                            ColumnLayout {
                                id: packagePage
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 28
                                spacing: 18

                                Card {
                                    Layout.fillWidth: true
                                    implicitHeight: 188
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 20
                                        spacing: 12
                                        RowLayout {
                                            Layout.fillWidth: true
                                            SectionTitle { text: "在线 Release" }
                                            Item { Layout.fillWidth: true }
                                            CheckBox { id: includePreviewCheck; text: "包含 Preview"; checked: true }
                                            Button {
                                                text: bootstrapController.packageRunning ? "刷新中…" : "刷新 Release"
                                                enabled: !bootstrapController.packageRunning
                                                onClicked: bootstrapController.refreshReleaseCatalog(includePreviewCheck.checked)
                                            }
                                        }
                                        MutedLabel { text: "来源：OpenAI 官方 GitHub Releases。下载只发生在当前 Windows 电脑，之后通过 SSH/SCP 部署到远端。" }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            ComboBox {
                                                id: packageManagerReleaseCombo
                                                Layout.fillWidth: true
                                                model: bootstrapController.releaseCatalog
                                                textRole: "label"
                                            }
                                            Button {
                                                text: "下载 ARM64"
                                                enabled: !bootstrapController.packageRunning && packageManagerReleaseCombo.currentIndex >= 0
                                                onClicked: {
                                                    const item = bootstrapController.releaseCatalog[packageManagerReleaseCombo.currentIndex]
                                                    if (item) bootstrapController.downloadPackage(item.tag, "arm64")
                                                }
                                            }
                                            Button {
                                                text: "下载 x86_64"
                                                enabled: !bootstrapController.packageRunning && packageManagerReleaseCombo.currentIndex >= 0
                                                onClicked: {
                                                    const item = bootstrapController.releaseCatalog[packageManagerReleaseCombo.currentIndex]
                                                    if (item) bootstrapController.downloadPackage(item.tag, "x86_64")
                                                }
                                            }
                                            Button {
                                                text: "两种都下载"
                                                highlighted: true
                                                enabled: !bootstrapController.packageRunning && packageManagerReleaseCombo.currentIndex >= 0
                                                onClicked: {
                                                    const item = bootstrapController.releaseCatalog[packageManagerReleaseCombo.currentIndex]
                                                    if (item) bootstrapController.downloadPackage(item.tag, "both")
                                                }
                                            }
                                        }
                                        ProgressBar {
                                            Layout.fillWidth: true
                                            from: 0; to: 100
                                            value: root.packageProgress
                                            visible: bootstrapController.packageRunning || root.packageProgress > 0
                                        }
                                        MutedLabel { text: root.packageStatusText; elide: Text.ElideRight; Layout.fillWidth: true }
                                    }
                                }

                                Card {
                                    Layout.fillWidth: true
                                    implicitHeight: Math.max(350, localPackageList.contentHeight + 118)
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 20
                                        spacing: 12
                                        RowLayout {
                                            Layout.fillWidth: true
                                            SectionTitle { text: "本地缓存" }
                                            Item { Layout.fillWidth: true }
                                            Label {
                                                text: bootstrapController.localPackages.length + " 个文件"
                                                color: "#8B92A0"
                                                font.pixelSize: 11
                                            }
                                            Button { text: "打开目录"; onClicked: bootstrapController.openPackageFolder() }
                                            Button { text: "刷新"; onClicked: bootstrapController.refreshLocalPackages() }
                                        }
                                        Rectangle {
                                            Layout.fillWidth: true
                                            implicitHeight: 42
                                            radius: 9
                                            color: "#F7F8FB"
                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.leftMargin: 12
                                                anchors.rightMargin: 12
                                                Label { text: "缓存目录"; color: "#7B8291"; font.pixelSize: 10 }
                                                Label { Layout.fillWidth: true; text: bootstrapController.defaultPackageDir; color: "#4C5360"; font.pixelSize: 10; elide: Text.ElideMiddle }
                                            }
                                        }

                                        ListView {
                                            id: localPackageList
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            implicitHeight: Math.max(220, contentHeight)
                                            spacing: 8
                                            clip: true
                                            model: bootstrapController.localPackages
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: localPackageList.width
                                                height: 64
                                                radius: 11
                                                color: "#FAFAFC"
                                                border.color: "#ECEEF3"
                                                RowLayout {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 14
                                                    anchors.rightMargin: 10
                                                    spacing: 14
                                                    Rectangle {
                                                        width: 38; height: 38; radius: 10
                                                        color: modelData.arch === "arm64" ? "#EEF0FF" : "#EAF8F3"
                                                        Label {
                                                            anchors.centerIn: parent
                                                            text: modelData.arch === "arm64" ? "ARM" : "x64"
                                                            color: modelData.arch === "arm64" ? "#6650E8" : "#15835C"
                                                            font.pixelSize: 10
                                                            font.bold: true
                                                        }
                                                    }
                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 2
                                                        Label { text: modelData.tag; color: "#242733"; font.pixelSize: 12; font.bold: true }
                                                        Label { text: modelData.fileName; color: "#858B99"; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                                    }
                                                    Label { text: modelData.size; color: "#5F6674"; font.pixelSize: 11; Layout.preferredWidth: 70 }
                                                    Label { text: modelData.modified; color: "#9096A3"; font.pixelSize: 10; Layout.preferredWidth: 105 }
                                                    Button { text: "删除"; flat: true; onClicked: bootstrapController.deleteLocalPackage(modelData.filePath) }
                                                }
                                            }
                                            Label {
                                                anchors.centerIn: parent
                                                visible: localPackageList.count === 0
                                                text: "还没有本地安装包\n从上方选择 Release 下载即可"
                                                horizontalAlignment: Text.AlignHCenter
                                                color: "#A0A6B2"
                                                font.pixelSize: 12
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Page 2: Logs
                    Item {
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 28
                            radius: 16
                            color: "#12141D"
                            border.color: "#222633"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 10
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: "运行日志"; color: "#F4F6FA"; font.pixelSize: 14; font.bold: true }
                                    Item { Layout.fillWidth: true }
                                    Button { text: "清空"; flat: true; onClicked: logArea.clear() }
                                }
                                ScrollView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    TextArea {
                                        id: logArea
                                        readOnly: true
                                        selectByMouse: true
                                        wrapMode: TextEdit.WrapAnywhere
                                        color: "#CDD2DC"
                                        selectionColor: "#6950E9"
                                        selectedTextColor: "white"
                                        font.family: "Consolas"
                                        font.pixelSize: 12
                                        background: Rectangle { color: "transparent" }
                                        text: "Codex Remote Manager ready.\n"
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        root.rebuildVersionModel()
        deviceCombo.currentIndex = 0
        root.applySshSelection()
        bootstrapController.refreshReleaseCatalog(true)
    }
}
