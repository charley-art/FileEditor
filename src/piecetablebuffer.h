#ifndef PIECETABLEBUFFER_H
#define PIECETABLEBUFFER_H

#include <functional>
#include <QString>
#include <QVector>

class PieceTableBuffer
{
public:
    struct SnapshotSegment {
        bool fromAddBuffer = false;
        int start = 0;
        int length = 0;
    };

    struct SaveSnapshot {
        QString original;
        QString add;
        QVector<SnapshotSegment> segments;
    };

    PieceTableBuffer();

    void reset(const QString &text);
    int length() const;
    bool isEmpty() const;

    void insert(int position, const QString &text);
    void remove(int position, int length);

    QChar at(int position) const;
    QString mid(int position, int length) const;
    QString toString() const;
    SaveSnapshot makeSaveSnapshot() const;
    bool forEachSegment(const std::function<bool(const QString &source, int start, int length)> &visitor) const;

private:
    enum class BufferKind {
        Original,
        Add
    };

    struct Piece {
        BufferKind kind = BufferKind::Original;
        int start = 0;
        int length = 0;
    };

    struct LocateResult {
        int pieceIndex = 0;
        int pieceInnerOffset = 0;
        int pieceGlobalStart = 0;
    };

    LocateResult locate(int position) const;
    void markPieceOffsetsDirty();
    void rebuildPieceOffsets() const;
    int findPieceIndexByOffset(int position) const;
    int pieceStartOffsetAt(int index) const;
    int pieceEndOffsetAt(int index) const;
    void resetLocateHint();
    const QString &bufferByKind(BufferKind kind) const;
    void mergeAdjacent();
    void mergeAdjacentAround(int index);

    QString m_original;
    QString m_add;
    QVector<Piece> m_pieces;
    int m_totalLength = 0;

    mutable bool m_hasLocateHint = false;
    mutable int m_lastLocatePieceIndex = 0;
    mutable int m_lastLocatePieceGlobalStart = 0;
    mutable int m_lastLocatePieceGlobalEnd = 0;

    mutable QVector<int> m_pieceEndOffsets;
    mutable bool m_pieceOffsetsDirty = true;
};

#endif // PIECETABLEBUFFER_H
