#include "piecetablebuffer.h"

#include <algorithm>

PieceTableBuffer::PieceTableBuffer()
{
    markPieceOffsetsDirty();
    resetLocateHint();
}

void PieceTableBuffer::reset(const QString &text)
{
    m_original = text;
    m_add.clear();
    m_pieces.clear();
    m_totalLength = text.length();

    if (!text.isEmpty()) {
        Piece piece;
        piece.kind = BufferKind::Original;
        piece.start = 0;
        piece.length = text.length();
        m_pieces.push_back(piece);
    }

    markPieceOffsetsDirty();
    resetLocateHint();
}

int PieceTableBuffer::length() const
{
    return m_totalLength;
}

bool PieceTableBuffer::isEmpty() const
{
    return m_totalLength == 0;
}

void PieceTableBuffer::insert(int position, const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    position = std::max(0, std::min(position, m_totalLength));

    Piece inserted;
    inserted.kind = BufferKind::Add;
    inserted.start = m_add.length();
    inserted.length = text.length();
    m_add += text;

    if (m_pieces.isEmpty()) {
        m_pieces.push_back(inserted);
        m_totalLength += text.length();
        markPieceOffsetsDirty();
        resetLocateHint();
        return;
    }

    const LocateResult located = locate(position);
    const int index = located.pieceIndex;
    const int inner = located.pieceInnerOffset;
    int insertedIndex = index;

    if (index >= m_pieces.size()) {
        m_pieces.push_back(inserted);
        insertedIndex = m_pieces.size() - 1;
        m_totalLength += text.length();
        mergeAdjacentAround(insertedIndex);
        resetLocateHint();
        return;
    }

    Piece target = m_pieces.at(index);

    if (inner <= 0) {
        m_pieces.insert(index, inserted);
        insertedIndex = index;
    } else if (inner >= target.length) {
        m_pieces.insert(index + 1, inserted);
        insertedIndex = index + 1;
    } else {
        Piece left = target;
        left.length = inner;

        Piece right = target;
        right.start += inner;
        right.length -= inner;

        m_pieces[index] = left;
        m_pieces.insert(index + 1, inserted);
        m_pieces.insert(index + 2, right);
        insertedIndex = index + 1;
    }

    m_totalLength += text.length();
    mergeAdjacentAround(insertedIndex);
    resetLocateHint();
}

void PieceTableBuffer::remove(int position, int length)
{
    if (length <= 0 || m_totalLength <= 0) {
        return;
    }

    position = std::max(0, std::min(position, m_totalLength));
    const int end = std::max(position, std::min(position + length, m_totalLength));
    if (end <= position) {
        return;
    }

    LocateResult startLoc = locate(position);
    int startIndex = startLoc.pieceIndex;
    int startInner = startLoc.pieceInnerOffset;
    if (startIndex < 0 || startIndex >= m_pieces.size()) {
        return;
    }

    if (startInner >= m_pieces.at(startIndex).length) {
        ++startIndex;
        startInner = 0;
        if (startIndex >= m_pieces.size()) {
            return;
        }
    }

    LocateResult endLoc = locate(end);
    int endIndex = endLoc.pieceIndex;
    int endInner = endLoc.pieceInnerOffset;
    if (endIndex < 0) {
        endIndex = 0;
        endInner = 0;
    } else if (endIndex >= m_pieces.size()) {
        endIndex = m_pieces.size() - 1;
        endInner = m_pieces.at(endIndex).length;
    }

    QVector<Piece> replacement;
    replacement.reserve(2);

    if (startIndex == endIndex) {
        const Piece &piece = m_pieces.at(startIndex);
        const int safeStartInner = qBound(0, startInner, piece.length);
        const int safeEndInner = qBound(safeStartInner, endInner, piece.length);
        if (safeStartInner > 0) {
            Piece left = piece;
            left.length = safeStartInner;
            replacement.push_back(left);
        }
        if (safeEndInner < piece.length) {
            Piece right = piece;
            right.start += safeEndInner;
            right.length = piece.length - safeEndInner;
            replacement.push_back(right);
        }

        m_pieces.remove(startIndex);
        for (int i = replacement.size() - 1; i >= 0; --i) {
            m_pieces.insert(startIndex, replacement.at(i));
        }
    } else {
        const Piece &startPiece = m_pieces.at(startIndex);
        const int safeStartInner = qBound(0, startInner, startPiece.length);
        if (safeStartInner > 0) {
            Piece left = startPiece;
            left.length = safeStartInner;
            replacement.push_back(left);
        }

        const Piece &endPiece = m_pieces.at(endIndex);
        const int safeEndInner = qBound(0, endInner, endPiece.length);
        if (safeEndInner < endPiece.length) {
            Piece right = endPiece;
            right.start += safeEndInner;
            right.length = endPiece.length - safeEndInner;
            replacement.push_back(right);
        }

        const int removeCount = qMax(0, endIndex - startIndex + 1);
        if (removeCount > 0) {
            m_pieces.remove(startIndex, removeCount);
        }
        for (int i = replacement.size() - 1; i >= 0; --i) {
            m_pieces.insert(startIndex, replacement.at(i));
        }
    }

    m_totalLength -= (end - position);
    if (!m_pieces.isEmpty()) {
        mergeAdjacentAround(qBound(0, startIndex, m_pieces.size() - 1));
    } else {
        markPieceOffsetsDirty();
    }
    resetLocateHint();
}

QChar PieceTableBuffer::at(int position) const
{
    if (position < 0 || position >= m_totalLength || m_pieces.isEmpty()) {
        return QChar();
    }

    const LocateResult located = locate(position);
    int pieceIndex = located.pieceIndex;
    int pieceInnerOffset = located.pieceInnerOffset;
    if (pieceIndex < 0 || pieceIndex >= m_pieces.size()) {
        return QChar();
    }

    const Piece *piece = &m_pieces.at(pieceIndex);
    if (pieceInnerOffset >= piece->length && pieceIndex + 1 < m_pieces.size()) {
        ++pieceIndex;
        pieceInnerOffset = 0;
        piece = &m_pieces.at(pieceIndex);
    }

    const QString &src = bufferByKind(piece->kind);
    const int srcIndex = piece->start + pieceInnerOffset;
    if (srcIndex < 0 || srcIndex >= src.length()) {
        return QChar();
    }

    return src.at(srcIndex);
}

QString PieceTableBuffer::mid(int position, int length) const
{
    if (length <= 0 || m_totalLength <= 0) {
        return {};
    }

    position = std::max(0, std::min(position, m_totalLength));
    const int end = std::max(position, std::min(position + length, m_totalLength));
    if (end <= position) {
        return {};
    }

    QString out;
    out.reserve(end - position);

    const LocateResult located = locate(position);
    if (located.pieceIndex >= m_pieces.size()) {
        return out;
    }

    int cursor = located.pieceGlobalStart;
    for (int i = located.pieceIndex; i < m_pieces.size(); ++i) {
        const Piece &piece = m_pieces.at(i);
        const int pieceStart = cursor;
        const int pieceEnd = cursor + piece.length;

        if (pieceEnd <= position) {
            cursor = pieceEnd;
            continue;
        }

        if (pieceStart >= end) {
            break;
        }

        const int takeFrom = std::max(position, pieceStart);
        const int takeTo = std::min(end, pieceEnd);
        const int fromInPiece = takeFrom - pieceStart;
        const int takeLen = takeTo - takeFrom;

        if (takeLen > 0) {
            const QString &src = bufferByKind(piece.kind);
            out.append(src.constData() + piece.start + fromInPiece, takeLen);
        }

        cursor = pieceEnd;
    }

    return out;
}

QString PieceTableBuffer::toString() const
{
    if (m_totalLength <= 0 || m_pieces.isEmpty()) {
        return {};
    }

    QString out;
    out.reserve(m_totalLength);
    for (const Piece &piece : m_pieces) {
        if (piece.length <= 0) {
            continue;
        }
        const QString &src = bufferByKind(piece.kind);
        out.append(src.constData() + piece.start, piece.length);
    }
    return out;
}

PieceTableBuffer::SaveSnapshot PieceTableBuffer::makeSaveSnapshot() const
{
    SaveSnapshot snapshot;
    snapshot.original = m_original;
    snapshot.add = m_add;
    snapshot.segments.reserve(m_pieces.size());

    for (const Piece &piece : m_pieces) {
        if (piece.length <= 0) {
            continue;
        }
        SnapshotSegment segment;
        segment.fromAddBuffer = (piece.kind == BufferKind::Add);
        segment.start = piece.start;
        segment.length = piece.length;
        snapshot.segments.push_back(segment);
    }

    return snapshot;
}

bool PieceTableBuffer::forEachSegment(
    const std::function<bool(const QString &source, int start, int length)> &visitor) const
{
    if (!visitor) {
        return true;
    }

    for (const Piece &piece : m_pieces) {
        if (piece.length <= 0) {
            continue;
        }
        const QString &src = bufferByKind(piece.kind);
        if (!visitor(src, piece.start, piece.length)) {
            return false;
        }
    }
    return true;
}

PieceTableBuffer::LocateResult PieceTableBuffer::locate(int position) const
{
    if (m_pieces.isEmpty()) {
        return {};
    }

    position = std::max(0, std::min(position, m_totalLength));

    auto makeResult = [&](int index, int pieceStart, int pieceEnd) -> LocateResult {
        LocateResult result;
        const Piece &piece = m_pieces.at(index);
        result.pieceIndex = index;
        result.pieceGlobalStart = pieceStart;
        result.pieceInnerOffset = qBound(0, position - pieceStart, piece.length);

        m_hasLocateHint = true;
        m_lastLocatePieceIndex = index;
        m_lastLocatePieceGlobalStart = pieceStart;
        m_lastLocatePieceGlobalEnd = pieceEnd;
        return result;
    };

    if (m_hasLocateHint
        && m_lastLocatePieceIndex >= 0
        && m_lastLocatePieceIndex < m_pieces.size()) {
        if (position >= m_lastLocatePieceGlobalStart && position <= m_lastLocatePieceGlobalEnd) {
            return makeResult(m_lastLocatePieceIndex,
                              m_lastLocatePieceGlobalStart,
                              m_lastLocatePieceGlobalEnd);
        }

        if (position > m_lastLocatePieceGlobalEnd) {
            int cursor = m_lastLocatePieceGlobalEnd;
            for (int i = m_lastLocatePieceIndex + 1; i < m_pieces.size(); ++i) {
                const Piece &piece = m_pieces.at(i);
                const int pieceEnd = cursor + piece.length;
                if (position <= pieceEnd) {
                    return makeResult(i, cursor, pieceEnd);
                }
                cursor = pieceEnd;
            }
        } else if (position < m_lastLocatePieceGlobalStart) {
            int pieceEnd = m_lastLocatePieceGlobalStart;
            for (int i = m_lastLocatePieceIndex - 1; i >= 0; --i) {
                const Piece &piece = m_pieces.at(i);
                const int pieceStart = pieceEnd - piece.length;
                if (position >= pieceStart) {
                    return makeResult(i, pieceStart, pieceEnd);
                }
                pieceEnd = pieceStart;
            }
        }
    }

    const int index = findPieceIndexByOffset(position);
    if (index >= 0 && index < m_pieces.size()) {
        return makeResult(index, pieceStartOffsetAt(index), pieceEndOffsetAt(index));
    }

    LocateResult tail;
    tail.pieceIndex = m_pieces.size();
    tail.pieceInnerOffset = 0;
    tail.pieceGlobalStart = m_totalLength;
    m_hasLocateHint = false;
    return tail;
}

const QString &PieceTableBuffer::bufferByKind(BufferKind kind) const
{
    return kind == BufferKind::Original ? m_original : m_add;
}

void PieceTableBuffer::mergeAdjacent()
{
    if (m_pieces.size() < 2) {
        markPieceOffsetsDirty();
        return;
    }

    QVector<Piece> merged;
    merged.reserve(m_pieces.size());
    merged.push_back(m_pieces.first());

    for (int i = 1; i < m_pieces.size(); ++i) {
        const Piece &current = m_pieces.at(i);
        Piece &tail = merged.last();
        if (tail.kind == current.kind && tail.start + tail.length == current.start) {
            tail.length += current.length;
        } else if (current.length > 0) {
            merged.push_back(current);
        }
    }

    m_pieces = merged;
    markPieceOffsetsDirty();
}

void PieceTableBuffer::mergeAdjacentAround(int index)
{
    if (m_pieces.isEmpty()) {
        markPieceOffsetsDirty();
        return;
    }

    int pivot = qBound(0, index, m_pieces.size() - 1);

    while (pivot > 0) {
        const Piece &left = m_pieces.at(pivot - 1);
        const Piece &current = m_pieces.at(pivot);
        if (left.length <= 0) {
            m_pieces.remove(pivot - 1);
            --pivot;
            continue;
        }
        if (current.length <= 0) {
            m_pieces.remove(pivot);
            if (pivot >= m_pieces.size()) {
                pivot = m_pieces.size() - 1;
            }
            if (m_pieces.isEmpty()) {
                break;
            }
            continue;
        }
        if (left.kind != current.kind || left.start + left.length != current.start) {
            break;
        }
        m_pieces[pivot - 1].length += current.length;
        m_pieces.remove(pivot);
        --pivot;
    }

    while (!m_pieces.isEmpty() && pivot >= 0 && pivot + 1 < m_pieces.size()) {
        const Piece &current = m_pieces.at(pivot);
        const Piece &next = m_pieces.at(pivot + 1);
        if (current.length <= 0) {
            m_pieces.remove(pivot);
            if (pivot >= m_pieces.size()) {
                pivot = m_pieces.size() - 1;
            }
            continue;
        }
        if (next.length <= 0) {
            m_pieces.remove(pivot + 1);
            continue;
        }
        if (current.kind != next.kind || current.start + current.length != next.start) {
            break;
        }
        m_pieces[pivot].length += next.length;
        m_pieces.remove(pivot + 1);
    }

    for (int i = m_pieces.size() - 1; i >= 0; --i) {
        if (m_pieces.at(i).length <= 0) {
            m_pieces.remove(i);
        }
    }

    markPieceOffsetsDirty();
}

void PieceTableBuffer::resetLocateHint()
{
    m_hasLocateHint = false;
    m_lastLocatePieceIndex = 0;
    m_lastLocatePieceGlobalStart = 0;
    m_lastLocatePieceGlobalEnd = 0;
}

void PieceTableBuffer::markPieceOffsetsDirty()
{
    m_pieceOffsetsDirty = true;
}

void PieceTableBuffer::rebuildPieceOffsets() const
{
    if (!m_pieceOffsetsDirty) {
        return;
    }

    m_pieceEndOffsets.resize(m_pieces.size());
    int cursor = 0;
    for (int i = 0; i < m_pieces.size(); ++i) {
        cursor += m_pieces.at(i).length;
        m_pieceEndOffsets[i] = cursor;
    }
    m_pieceOffsetsDirty = false;
}

int PieceTableBuffer::findPieceIndexByOffset(int position) const
{
    if (m_pieces.isEmpty()) {
        return -1;
    }

    rebuildPieceOffsets();
    auto it = std::lower_bound(m_pieceEndOffsets.cbegin(), m_pieceEndOffsets.cend(), position);
    if (it == m_pieceEndOffsets.cend()) {
        return m_pieces.size() - 1;
    }
    return static_cast<int>(std::distance(m_pieceEndOffsets.cbegin(), it));
}

int PieceTableBuffer::pieceStartOffsetAt(int index) const
{
    if (index <= 0) {
        return 0;
    }

    rebuildPieceOffsets();
    return m_pieceEndOffsets.at(index - 1);
}

int PieceTableBuffer::pieceEndOffsetAt(int index) const
{
    rebuildPieceOffsets();
    return m_pieceEndOffsets.at(index);
}
