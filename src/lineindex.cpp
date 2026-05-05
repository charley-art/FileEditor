#include "lineindex.h"

#include <QRandomGenerator>
#include <QtGlobal>

#include <algorithm>

LineIndex::LineIndex()
{
    m_root = newNode(0);
    Q_ASSERT(isValid());
}

LineIndex::~LineIndex()
{
    clearTree();
}

int LineIndex::nodeSize(const Node *node)
{
    return node ? node->size : 0;
}

void LineIndex::pull(Node *node)
{
    if (!node) {
        return;
    }
    node->size = 1 + nodeSize(node->left) + nodeSize(node->right);
}

void LineIndex::applyLazy(Node *node, int delta)
{
    if (!node || delta == 0) {
        return;
    }
    node->key += delta;
    node->lazy += delta;
}

void LineIndex::push(Node *node)
{
    if (!node || node->lazy == 0) {
        return;
    }
    applyLazy(node->left, node->lazy);
    applyLazy(node->right, node->lazy);
    node->lazy = 0;
}

LineIndex::Node *LineIndex::newNode(int key)
{
    Node *node = new Node;
    node->key = key;
    node->priority = QRandomGenerator::global()->generate();
    node->size = 1;
    node->lazy = 0;
    node->left = nullptr;
    node->right = nullptr;
    return node;
}

LineIndex::Node *LineIndex::merge(Node *left, Node *right)
{
    if (!left) {
        return right;
    }
    if (!right) {
        return left;
    }

    if (left->priority > right->priority) {
        push(left);
        left->right = merge(left->right, right);
        pull(left);
        return left;
    }

    push(right);
    right->left = merge(left, right->left);
    pull(right);
    return right;
}

void LineIndex::splitByKey(Node *root, int key, Node *&left, Node *&right)
{
    if (!root) {
        left = nullptr;
        right = nullptr;
        return;
    }

    push(root);
    if (root->key <= key) {
        splitByKey(root->right, key, root->right, right);
        left = root;
        pull(left);
        return;
    }

    splitByKey(root->left, key, left, root->left);
    right = root;
    pull(right);
}

void LineIndex::deleteSubtree(Node *root)
{
    if (!root) {
        return;
    }

    QVector<Node *> stack;
    stack.reserve(1024);
    stack.push_back(root);
    while (!stack.isEmpty()) {
        Node *node = stack.takeLast();
        if (node->left) {
            stack.push_back(node->left);
        }
        if (node->right) {
            stack.push_back(node->right);
        }
        delete node;
    }
}

void LineIndex::clearTree()
{
    deleteSubtree(m_root);
    m_root = nullptr;
}

LineIndex::Node *LineIndex::buildFromSortedKeys(const QVector<int> &keys)
{
    if (keys.isEmpty()) {
        return nullptr;
    }

    QVector<Node *> stack;
    stack.reserve(keys.size());

    for (int key : keys) {
        Node *node = newNode(key);
        Node *lastPopped = nullptr;

        while (!stack.isEmpty() && stack.last()->priority < node->priority) {
            lastPopped = stack.takeLast();
        }

        node->left = lastPopped;
        if (!stack.isEmpty()) {
            stack.last()->right = node;
        }
        stack.push_back(node);
    }

    Node *root = stack.first();

    QVector<Node *> dfs;
    QVector<Node *> postorder;
    dfs.reserve(keys.size());
    postorder.reserve(keys.size());
    dfs.push_back(root);

    while (!dfs.isEmpty()) {
        Node *node = dfs.takeLast();
        postorder.push_back(node);
        if (node->left) {
            dfs.push_back(node->left);
        }
        if (node->right) {
            dfs.push_back(node->right);
        }
    }

    for (int i = postorder.size() - 1; i >= 0; --i) {
        pull(postorder.at(i));
    }

    return root;
}

void LineIndex::rebuild(const QString &text)
{
    QVector<int> starts;
    starts.reserve(qMax(1, text.length() / 16));
    starts.push_back(0);

    for (int i = 0; i < text.length(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('\r')) {
            if (i + 1 < text.length() && text.at(i + 1) == QLatin1Char('\n')) {
                starts.push_back(i + 2);
                ++i;
            } else {
                starts.push_back(i + 1);
            }
        } else if (ch == QLatin1Char('\n')) {
            starts.push_back(i + 1);
        }
    }

    clearTree();
    m_root = buildFromSortedKeys(starts);
    if (!m_root) {
        m_root = newNode(0);
    }

    m_textLength = text.length();
    Q_ASSERT(isValid());
}

void LineIndex::applyInsert(int position, const QString &insertedText)
{
    const int insertedLen = insertedText.length();
    if (insertedLen <= 0) {
        return;
    }

    position = std::max(0, std::min(position, m_textLength));

    QVector<int> newLineStarts;
    for (int i = 0; i < insertedLen; ++i) {
        const QChar ch = insertedText.at(i);
        if (ch == QLatin1Char('\r')) {
            if (i + 1 < insertedLen && insertedText.at(i + 1) == QLatin1Char('\n')) {
                newLineStarts.push_back(position + i + 2);
                ++i;
            } else {
                newLineStarts.push_back(position + i + 1);
            }
        } else if (ch == QLatin1Char('\n')) {
            newLineStarts.push_back(position + i + 1);
        }
    }

    if (newLineStarts.isEmpty()) {
        shiftStartsFrom(position, insertedLen);
        m_textLength += insertedLen;
        Q_ASSERT(isFastValid());
        return;
    }

    Node *left = nullptr;
    Node *right = nullptr;
    splitByKey(m_root, position, left, right);
    applyLazy(right, insertedLen);

    Node *middle = nullptr;
    for (int start : newLineStarts) {
        middle = merge(middle, newNode(start));
    }

    m_root = merge(merge(left, middle), right);
    if (!m_root) {
        m_root = newNode(0);
    }

    m_textLength += insertedLen;
    Q_ASSERT(isValid());
}

void LineIndex::applyDelete(int position, int length)
{
    if (length <= 0 || m_textLength <= 0) {
        return;
    }

    position = std::max(0, std::min(position, m_textLength));
    const int end = std::max(position, std::min(position + length, m_textLength));
    if (end <= position) {
        return;
    }

    const int deletedLen = end - position;
    const int firstRemoved = upperBoundAdjusted(position, 1);
    const int afterRemoved = upperBoundAdjusted(end, firstRemoved);
    if (firstRemoved == afterRemoved) {
        shiftStartsFrom(end, -deletedLen);
        m_textLength -= deletedLen;
        Q_ASSERT(isFastValid());
        return;
    }

    Node *left = nullptr;
    Node *rest = nullptr;
    splitByKey(m_root, position, left, rest);

    Node *toDelete = nullptr;
    Node *right = nullptr;
    splitByKey(rest, end, toDelete, right);
    deleteSubtree(toDelete);

    applyLazy(right, -deletedLen);
    m_root = merge(left, right);
    if (!m_root) {
        m_root = newNode(0);
    }

    m_textLength -= deletedLen;
    Q_ASSERT(isValid());
}

int LineIndex::lineCount() const
{
    return nodeSize(m_root);
}

int LineIndex::countLessOrEqual(int key) const
{
    int count = 0;
    const Node *node = m_root;
    int carry = 0;

    while (node) {
        const int current = node->key + carry;
        const int nextCarry = carry + node->lazy;
        if (current <= key) {
            count += nodeSize(node->left) + 1;
            node = node->right;
            carry = nextCarry;
        } else {
            node = node->left;
            carry = nextCarry;
        }
    }

    return count;
}

int LineIndex::upperBoundAdjusted(int offset, int beginIndex) const
{
    const int upper = countLessOrEqual(offset);
    return qMax(beginIndex, upper);
}

int LineIndex::lineForOffset(int offset) const
{
    if (!m_root) {
        return 0;
    }

    offset = std::max(0, std::min(offset, m_textLength));
    const int upper = upperBoundAdjusted(offset);
    if (upper <= 0) {
        return 0;
    }
    return upper - 1;
}

int LineIndex::keyAtIndex(int index) const
{
    if (!m_root) {
        return 0;
    }

    index = qBound(0, index, lineCount() - 1);
    const Node *node = m_root;
    int carry = 0;

    while (node) {
        const int nextCarry = carry + node->lazy;
        const int leftSize = nodeSize(node->left);
        if (index < leftSize) {
            node = node->left;
            carry = nextCarry;
            continue;
        }
        if (index == leftSize) {
            return node->key + carry;
        }
        index -= leftSize + 1;
        node = node->right;
        carry = nextCarry;
    }

    return 0;
}

int LineIndex::lineStart(int line) const
{
    if (!m_root) {
        return 0;
    }
    return keyAtIndex(line);
}

int LineIndex::lineEndExclusive(int line) const
{
    if (!m_root) {
        return 0;
    }

    const int count = lineCount();
    if (count <= 0) {
        return 0;
    }

    line = qBound(0, line, count - 1);
    if (line + 1 < count) {
        return keyAtIndex(line + 1);
    }
    return m_textLength;
}

int LineIndex::textLength() const
{
    return m_textLength;
}

void LineIndex::shiftStartsFrom(int startOffset, int delta)
{
    if (delta == 0 || lineCount() <= 1) {
        return;
    }

    Node *left = nullptr;
    Node *right = nullptr;
    splitByKey(m_root, startOffset, left, right);
    applyLazy(right, delta);
    m_root = merge(left, right);
}

bool LineIndex::isFastValid() const
{
    if (m_textLength < 0) {
        return false;
    }
    if (!m_root) {
        return false;
    }
    if (lineCount() <= 0) {
        return false;
    }
    if (lineStart(0) != 0) {
        return false;
    }
    return true;
}

bool LineIndex::isValid() const
{
    if (!isFastValid()) {
        return false;
    }

    const int count = lineCount();
    const int maxFullCheck = 200000;
    int prev = -1;

    if (count <= maxFullCheck) {
        for (int i = 0; i < count; ++i) {
            const int start = keyAtIndex(i);
            if (start < 0 || start > m_textLength) {
                return false;
            }
            if (start <= prev) {
                return false;
            }
            prev = start;
        }
        return true;
    }

    const int step = qMax(1, (count - 1) / 4096);
    for (int i = 0; i < count; i += step) {
        const int start = keyAtIndex(i);
        if (start < 0 || start > m_textLength) {
            return false;
        }
        if (start <= prev) {
            return false;
        }
        prev = start;
    }

    const int last = keyAtIndex(count - 1);
    if (last < 0 || last > m_textLength) {
        return false;
    }
    if (last <= prev) {
        return false;
    }

    return true;
}
