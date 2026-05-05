#ifndef LINEINDEX_H
#define LINEINDEX_H

#include <QString>
#include <QVector>

class LineIndex
{
public:
    LineIndex();
    ~LineIndex();

    LineIndex(const LineIndex &) = delete;
    LineIndex &operator=(const LineIndex &) = delete;

    void rebuild(const QString &text);
    void applyInsert(int position, const QString &insertedText);
    void applyDelete(int position, int length);

    int lineCount() const;
    int lineForOffset(int offset) const;
    int lineStart(int line) const;
    int lineEndExclusive(int line) const;
    int textLength() const;

private:
    struct Node {
        int key = 0;
        quint32 priority = 0;
        int size = 1;
        int lazy = 0;
        Node *left = nullptr;
        Node *right = nullptr;
    };

    static int nodeSize(const Node *node);
    static void pull(Node *node);
    static void applyLazy(Node *node, int delta);
    static void push(Node *node);

    Node *newNode(int key);
    Node *merge(Node *left, Node *right);
    void splitByKey(Node *root, int key, Node *&left, Node *&right);
    void clearTree();
    static void deleteSubtree(Node *root);
    Node *buildFromSortedKeys(const QVector<int> &keys);

    void shiftStartsFrom(int startOffset, int delta);
    int upperBoundAdjusted(int offset, int beginIndex = 0) const;
    int countLessOrEqual(int key) const;
    int keyAtIndex(int index) const;

    bool isFastValid() const;
    bool isValid() const;

    Node *m_root = nullptr;
    int m_textLength = 0;
};

#endif // LINEINDEX_H
