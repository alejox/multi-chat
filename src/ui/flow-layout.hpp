#pragma once

// Layout que coloca los widgets en fila y salta de línea cuando no caben
// (basado en el ejemplo "Flow Layout" de Qt).

#include <QLayout>
#include <QList>

class FlowLayout : public QLayout {
public:
	explicit FlowLayout(QWidget *parent = nullptr, int spacing = 4);
	~FlowLayout() override;

	void addItem(QLayoutItem *item) override;
	int count() const override { return int(items_.size()); }
	QLayoutItem *itemAt(int index) const override { return items_.value(index); }
	QLayoutItem *takeAt(int index) override;
	Qt::Orientations expandingDirections() const override { return {}; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }
	QSize minimumSize() const override;
	QSize sizeHint() const override { return minimumSize(); }
	void setGeometry(const QRect &rect) override;

private:
	int doLayout(const QRect &rect, bool testOnly) const;

	QList<QLayoutItem *> items_;
	int spacing_;
};
