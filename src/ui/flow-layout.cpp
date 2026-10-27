#include "flow-layout.hpp"

#include <QWidget>

#include <algorithm>

FlowLayout::FlowLayout(QWidget *parent, int spacing) : QLayout(parent), spacing_(spacing)
{
	setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
	while (QLayoutItem *item = takeAt(0))
		delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
	items_.append(item);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
	if (index < 0 || index >= items_.size())
		return nullptr;
	return items_.takeAt(index);
}

QSize FlowLayout::minimumSize() const
{
	QSize size;
	for (const QLayoutItem *item : items_)
		size = size.expandedTo(item->minimumSize());
	const QMargins m = contentsMargins();
	return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

void FlowLayout::setGeometry(const QRect &rect)
{
	QLayout::setGeometry(rect);
	doLayout(rect, false);
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
	const QMargins m = contentsMargins();
	const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
	int x = area.x();
	int y = area.y();
	int lineHeight = 0;
	for (QLayoutItem *item : items_) {
		if (item->widget() && item->widget()->isHidden())
			continue;
		const QSize hint = item->sizeHint();
		int nextX = x + hint.width() + spacing_;
		if (nextX - spacing_ > area.right() + 1 && lineHeight > 0) {
			x = area.x();
			y += lineHeight + spacing_;
			nextX = x + hint.width() + spacing_;
			lineHeight = 0;
		}
		if (!testOnly)
			item->setGeometry(
				QRect(QPoint(x, y), QSize(std::min(hint.width(), area.width()), hint.height())));
		x = nextX;
		lineHeight = std::max(lineHeight, hint.height());
	}
	return y + lineHeight - rect.y() + m.bottom();
}
