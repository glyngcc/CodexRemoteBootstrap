/****************************************************************************
** Meta object code from reading C++ file 'bootstrapcontroller.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../bootstrapcontroller.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'bootstrapcontroller.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.9.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN19BootstrapControllerE_t {};
} // unnamed namespace

template <> constexpr inline auto BootstrapController::qt_create_metaobjectdata<qt_meta_tag_ZN19BootstrapControllerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "BootstrapController",
        "runningChanged",
        "",
        "packageRunningChanged",
        "sshHostsChanged",
        "releaseCatalogChanged",
        "localPackagesChanged",
        "logLine",
        "line",
        "stepChanged",
        "step",
        "percent",
        "completed",
        "ok",
        "message",
        "packageStatus",
        "packageCompleted",
        "testConnection",
        "QVariantMap",
        "options",
        "configure",
        "suggestedAlias",
        "host",
        "localPathFromUrl",
        "url",
        "refreshSshHosts",
        "sshHostAt",
        "index",
        "refreshReleaseCatalog",
        "includePrerelease",
        "refreshLocalPackages",
        "downloadPackage",
        "releaseSelector",
        "arch",
        "deleteLocalPackage",
        "filePath",
        "openPackageFolder",
        "running",
        "packageRunning",
        "defaultIdentityPath",
        "defaultPackageDir",
        "sshConfigPath",
        "sshHosts",
        "QVariantList",
        "releaseCatalog",
        "localPackages"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'runningChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'packageRunningChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'sshHostsChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'releaseCatalogChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'localPackagesChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'logLine'
        QtMocHelpers::SignalData<void(const QString &)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 },
        }}),
        // Signal 'stepChanged'
        QtMocHelpers::SignalData<void(const QString &, int)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 }, { QMetaType::Int, 11 },
        }}),
        // Signal 'completed'
        QtMocHelpers::SignalData<void(bool, const QString &)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 13 }, { QMetaType::QString, 14 },
        }}),
        // Signal 'packageStatus'
        QtMocHelpers::SignalData<void(const QString &, int)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 14 }, { QMetaType::Int, 11 },
        }}),
        // Signal 'packageCompleted'
        QtMocHelpers::SignalData<void(bool, const QString &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 13 }, { QMetaType::QString, 14 },
        }}),
        // Method 'testConnection'
        QtMocHelpers::MethodData<void(const QVariantMap &)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 18, 19 },
        }}),
        // Method 'configure'
        QtMocHelpers::MethodData<void(const QVariantMap &)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 18, 19 },
        }}),
        // Method 'suggestedAlias'
        QtMocHelpers::MethodData<QString(const QString &) const>(21, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 22 },
        }}),
        // Method 'localPathFromUrl'
        QtMocHelpers::MethodData<QString(const QUrl &) const>(23, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QUrl, 24 },
        }}),
        // Method 'refreshSshHosts'
        QtMocHelpers::MethodData<void()>(25, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'sshHostAt'
        QtMocHelpers::MethodData<QVariantMap(int) const>(26, 2, QMC::AccessPublic, 0x80000000 | 18, {{
            { QMetaType::Int, 27 },
        }}),
        // Method 'refreshReleaseCatalog'
        QtMocHelpers::MethodData<void(bool)>(28, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 29 },
        }}),
        // Method 'refreshReleaseCatalog'
        QtMocHelpers::MethodData<void()>(28, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void),
        // Method 'refreshLocalPackages'
        QtMocHelpers::MethodData<void()>(30, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'downloadPackage'
        QtMocHelpers::MethodData<void(const QString &, const QString &)>(31, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 32 }, { QMetaType::QString, 33 },
        }}),
        // Method 'deleteLocalPackage'
        QtMocHelpers::MethodData<void(const QString &)>(34, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 35 },
        }}),
        // Method 'openPackageFolder'
        QtMocHelpers::MethodData<void()>(36, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'running'
        QtMocHelpers::PropertyData<bool>(37, QMetaType::Bool, QMC::DefaultPropertyFlags, 0),
        // property 'packageRunning'
        QtMocHelpers::PropertyData<bool>(38, QMetaType::Bool, QMC::DefaultPropertyFlags, 1),
        // property 'defaultIdentityPath'
        QtMocHelpers::PropertyData<QString>(39, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Constant),
        // property 'defaultPackageDir'
        QtMocHelpers::PropertyData<QString>(40, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Constant),
        // property 'sshConfigPath'
        QtMocHelpers::PropertyData<QString>(41, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Constant),
        // property 'sshHosts'
        QtMocHelpers::PropertyData<QVariantList>(42, 0x80000000 | 43, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 2),
        // property 'releaseCatalog'
        QtMocHelpers::PropertyData<QVariantList>(44, 0x80000000 | 43, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 3),
        // property 'localPackages'
        QtMocHelpers::PropertyData<QVariantList>(45, 0x80000000 | 43, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 4),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<BootstrapController, qt_meta_tag_ZN19BootstrapControllerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject BootstrapController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19BootstrapControllerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19BootstrapControllerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN19BootstrapControllerE_t>.metaTypes,
    nullptr
} };

void BootstrapController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<BootstrapController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->runningChanged(); break;
        case 1: _t->packageRunningChanged(); break;
        case 2: _t->sshHostsChanged(); break;
        case 3: _t->releaseCatalogChanged(); break;
        case 4: _t->localPackagesChanged(); break;
        case 5: _t->logLine((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->stepChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 7: _t->completed((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 8: _t->packageStatus((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 9: _t->packageCompleted((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 10: _t->testConnection((*reinterpret_cast< std::add_pointer_t<QVariantMap>>(_a[1]))); break;
        case 11: _t->configure((*reinterpret_cast< std::add_pointer_t<QVariantMap>>(_a[1]))); break;
        case 12: { QString _r = _t->suggestedAlias((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 13: { QString _r = _t->localPathFromUrl((*reinterpret_cast< std::add_pointer_t<QUrl>>(_a[1])));
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 14: _t->refreshSshHosts(); break;
        case 15: { QVariantMap _r = _t->sshHostAt((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast< QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 16: _t->refreshReleaseCatalog((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 17: _t->refreshReleaseCatalog(); break;
        case 18: _t->refreshLocalPackages(); break;
        case 19: _t->downloadPackage((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 20: _t->deleteLocalPackage((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 21: _t->openPackageFolder(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (BootstrapController::*)()>(_a, &BootstrapController::runningChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (BootstrapController::*)()>(_a, &BootstrapController::packageRunningChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (BootstrapController::*)()>(_a, &BootstrapController::sshHostsChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (BootstrapController::*)()>(_a, &BootstrapController::releaseCatalogChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (BootstrapController::*)()>(_a, &BootstrapController::localPackagesChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (BootstrapController::*)(const QString & )>(_a, &BootstrapController::logLine, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (BootstrapController::*)(const QString & , int )>(_a, &BootstrapController::stepChanged, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (BootstrapController::*)(bool , const QString & )>(_a, &BootstrapController::completed, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (BootstrapController::*)(const QString & , int )>(_a, &BootstrapController::packageStatus, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (BootstrapController::*)(bool , const QString & )>(_a, &BootstrapController::packageCompleted, 9))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<bool*>(_v) = _t->running(); break;
        case 1: *reinterpret_cast<bool*>(_v) = _t->packageRunning(); break;
        case 2: *reinterpret_cast<QString*>(_v) = _t->defaultIdentityPath(); break;
        case 3: *reinterpret_cast<QString*>(_v) = _t->defaultPackageDir(); break;
        case 4: *reinterpret_cast<QString*>(_v) = _t->sshConfigPath(); break;
        case 5: *reinterpret_cast<QVariantList*>(_v) = _t->sshHosts(); break;
        case 6: *reinterpret_cast<QVariantList*>(_v) = _t->releaseCatalog(); break;
        case 7: *reinterpret_cast<QVariantList*>(_v) = _t->localPackages(); break;
        default: break;
        }
    }
}

const QMetaObject *BootstrapController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *BootstrapController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19BootstrapControllerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int BootstrapController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 22)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 22;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 22)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 22;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    return _id;
}

// SIGNAL 0
void BootstrapController::runningChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void BootstrapController::packageRunningChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void BootstrapController::sshHostsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void BootstrapController::releaseCatalogChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void BootstrapController::localPackagesChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void BootstrapController::logLine(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}

// SIGNAL 6
void BootstrapController::stepChanged(const QString & _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1, _t2);
}

// SIGNAL 7
void BootstrapController::completed(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1, _t2);
}

// SIGNAL 8
void BootstrapController::packageStatus(const QString & _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1, _t2);
}

// SIGNAL 9
void BootstrapController::packageCompleted(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1, _t2);
}
QT_WARNING_POP
