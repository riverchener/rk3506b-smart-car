#ifndef PLAYLISTMODEL_H
#define PLAYLISTMODEL_H

#include <QAbstractItemModel>
#include <QScopedPointer>

QT_BEGIN_NAMESPACE
class QMediaPlaylist;
QT_END_NAMESPACE

class PlaylistModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    // 定义一个枚举类型Column，用于表示播放列表在视图中展示时的列信息
    enum Column
    {
        // 第一列表示标题列，索引值为0
        Title = 0,
        // 用于表示列的总数，这里暂时只有标题列，所以值为1
        ColumnCount
    };


    explicit PlaylistModel(QObject *parent = nullptr);
    ~PlaylistModel();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;


    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QMediaPlaylist *playlist() const;
    // 设置关联的播放列表对象，传入一个QMediaPlaylist *类型的指针
    void setPlaylist(QMediaPlaylist *playlist);

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::DisplayRole) override;

private slots:
    // 当开始插入多个项目到播放列表时被调用，传入开始和结束的索引值，表示要插入的项目范围
    void beginInsertItems(int start, int end);
    // 当完成插入多个项目到播放列表时被调用
    void endInsertItems();
    // 当开始从播放列表中移除多个项目时被调用，传入开始和结束的索引值，表示要移除的项目范围
    void beginRemoveItems(int start, int end);
    // 当完成从播放列表中移除多个项目时被调用
    void endRemoveItems();
    // 当播放列表中的多个项目发生变化时被调用，传入开始和结束的索引值，表示发生变化的项目范围
    void changeItems(int start, int end);

private:
    // 使用QScopedPointer来管理QMediaPlaylist对象的生命周期，确保资源的安全释放
    QScopedPointer<QMediaPlaylist> m_playlist;
    // 使用QMap来存储每个模型索引对应的额外数据（除了播放列表本身的数据外），以便在需要时获取和使用
    QMap<QModelIndex, QVariant> m_data;
};

#endif // PLAYLISTMODEL_H
