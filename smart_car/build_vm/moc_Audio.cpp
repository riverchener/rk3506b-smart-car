/****************************************************************************
** Meta object code from reading C++ file 'Audio.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.18)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../Audio.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Audio.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.18. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_Audio_t {
    QByteArrayData data[23];
    char stringdata0[430];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Audio_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Audio_t qt_meta_stringdata_Audio = {
    {
QT_MOC_LITERAL(0, 0, 5), // "Audio"
QT_MOC_LITERAL(1, 6, 4), // "back"
QT_MOC_LITERAL(2, 11, 0), // ""
QT_MOC_LITERAL(3, 12, 19), // "on_btn_play_clicked"
QT_MOC_LITERAL(4, 32, 7), // "checked"
QT_MOC_LITERAL(5, 40, 18), // "on_btn_pre_clicked"
QT_MOC_LITERAL(6, 59, 19), // "on_btn_next_clicked"
QT_MOC_LITERAL(7, 79, 19), // "on_btn_loop_clicked"
QT_MOC_LITERAL(8, 99, 23), // "mediaPlayerStateChanged"
QT_MOC_LITERAL(9, 123, 19), // "QMediaPlayer::State"
QT_MOC_LITERAL(10, 143, 32), // "mediaPlaylistCurrentIndexChanged"
QT_MOC_LITERAL(11, 176, 26), // "musicPlayerDurationChanged"
QT_MOC_LITERAL(12, 203, 26), // "mediaPlayerPositionChanged"
QT_MOC_LITERAL(13, 230, 26), // "onMetaDataAvailableChanged"
QT_MOC_LITERAL(14, 257, 9), // "available"
QT_MOC_LITERAL(15, 267, 12), // "updateLyrics"
QT_MOC_LITERAL(16, 280, 11), // "currentTime"
QT_MOC_LITERAL(17, 292, 25), // "on_listWidget_itemClicked"
QT_MOC_LITERAL(18, 318, 16), // "QListWidgetItem*"
QT_MOC_LITERAL(19, 335, 4), // "item"
QT_MOC_LITERAL(20, 340, 34), // "on_horizontalSlider_sliderRel..."
QT_MOC_LITERAL(21, 375, 21), // "on_pushButton_clicked"
QT_MOC_LITERAL(22, 397, 32) // "on_verticalSlider_sliderReleased"

    },
    "Audio\0back\0\0on_btn_play_clicked\0checked\0"
    "on_btn_pre_clicked\0on_btn_next_clicked\0"
    "on_btn_loop_clicked\0mediaPlayerStateChanged\0"
    "QMediaPlayer::State\0"
    "mediaPlaylistCurrentIndexChanged\0"
    "musicPlayerDurationChanged\0"
    "mediaPlayerPositionChanged\0"
    "onMetaDataAvailableChanged\0available\0"
    "updateLyrics\0currentTime\0"
    "on_listWidget_itemClicked\0QListWidgetItem*\0"
    "item\0on_horizontalSlider_sliderReleased\0"
    "on_pushButton_clicked\0"
    "on_verticalSlider_sliderReleased"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Audio[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      15,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   89,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       3,    1,   90,    2, 0x08 /* Private */,
       5,    0,   93,    2, 0x08 /* Private */,
       6,    0,   94,    2, 0x08 /* Private */,
       7,    1,   95,    2, 0x08 /* Private */,
       8,    1,   98,    2, 0x08 /* Private */,
      10,    1,  101,    2, 0x08 /* Private */,
      11,    1,  104,    2, 0x08 /* Private */,
      12,    1,  107,    2, 0x08 /* Private */,
      13,    1,  110,    2, 0x08 /* Private */,
      15,    1,  113,    2, 0x08 /* Private */,
      17,    1,  116,    2, 0x08 /* Private */,
      20,    0,  119,    2, 0x08 /* Private */,
      21,    0,  120,    2, 0x08 /* Private */,
      22,    0,  121,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, QMetaType::Bool,    4,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,    4,
    QMetaType::Void, 0x80000000 | 9,    2,
    QMetaType::Void, QMetaType::Int,    2,
    QMetaType::Void, QMetaType::LongLong,    2,
    QMetaType::Void, QMetaType::LongLong,    2,
    QMetaType::Void, QMetaType::Bool,   14,
    QMetaType::Void, QMetaType::LongLong,   16,
    QMetaType::Void, 0x80000000 | 18,   19,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void Audio::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Audio *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->back(); break;
        case 1: _t->on_btn_play_clicked((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 2: _t->on_btn_pre_clicked(); break;
        case 3: _t->on_btn_next_clicked(); break;
        case 4: _t->on_btn_loop_clicked((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 5: _t->mediaPlayerStateChanged((*reinterpret_cast< QMediaPlayer::State(*)>(_a[1]))); break;
        case 6: _t->mediaPlaylistCurrentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 7: _t->musicPlayerDurationChanged((*reinterpret_cast< qint64(*)>(_a[1]))); break;
        case 8: _t->mediaPlayerPositionChanged((*reinterpret_cast< qint64(*)>(_a[1]))); break;
        case 9: _t->onMetaDataAvailableChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 10: _t->updateLyrics((*reinterpret_cast< qint64(*)>(_a[1]))); break;
        case 11: _t->on_listWidget_itemClicked((*reinterpret_cast< QListWidgetItem*(*)>(_a[1]))); break;
        case 12: _t->on_horizontalSlider_sliderReleased(); break;
        case 13: _t->on_pushButton_clicked(); break;
        case 14: _t->on_verticalSlider_sliderReleased(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 5:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QMediaPlayer::State >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Audio::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Audio::back)) {
                *result = 0;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject Audio::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_Audio.data,
    qt_meta_data_Audio,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Audio::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Audio::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Audio.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int Audio::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 15)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 15;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 15)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 15;
    }
    return _id;
}

// SIGNAL 0
void Audio::back()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
