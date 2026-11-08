#pragma once

#include <QListWidget>

#include <functional>

// The chat list: tells the dialog when a drag finished so the new order is saved.
class ChatList : public QListWidget {
public:
	using QListWidget::QListWidget;
	std::function<void()> onDropped;

protected:
	void dropEvent(QDropEvent *event) override;
};
