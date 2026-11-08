#include "chat-list.hpp"

#include <QDropEvent>

void ChatList::dropEvent(QDropEvent *event)
{
	QListWidget::dropEvent(event);
	if (onDropped)
		onDropped();
}
