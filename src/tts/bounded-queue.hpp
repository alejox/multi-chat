#pragma once

// Cola FIFO con límite: si está llena, se descarta el elemento más antiguo. En un chat rápido
// conviene leer lo último que se dijo y no acumular minutos de retraso.
// No es segura entre hilos: quien la usa pone el candado.

#include <cstddef>
#include <deque>
#include <utility>

namespace tts {

template<typename T> class BoundedQueue {
public:
	explicit BoundedQueue(std::size_t capacity = 20) : capacity_(capacity > 0 ? capacity : 1) {}

	// Añade al final. Devuelve cuántos elementos se descartaron con esta llamada.
	std::size_t push(T item)
	{
		items_.push_back(std::move(item));
		return trim();
	}

	// Saca el primero. Devuelve false si la cola está vacía.
	bool pop(T &out)
	{
		if (items_.empty())
			return false;
		out = std::move(items_.front());
		items_.pop_front();
		return true;
	}

	// Cambia el límite; si la cola ya lo supera, descarta los más antiguos.
	std::size_t setCapacity(std::size_t capacity)
	{
		capacity_ = capacity > 0 ? capacity : 1;
		return trim();
	}

	// Removes every item that matches, keeping the order of the rest. Returns how many it removed
	// (they do not count as dropped).
	template<typename Predicate> std::size_t removeIf(Predicate matches)
	{
		std::size_t removed = 0;
		for (auto it = items_.begin(); it != items_.end();) {
			if (matches(*it)) {
				it = items_.erase(it);
				++removed;
			} else {
				++it;
			}
		}
		return removed;
	}

	void clear() { items_.clear(); }
	bool empty() const { return items_.empty(); }
	std::size_t size() const { return items_.size(); }
	std::size_t capacity() const { return capacity_; }
	// Total descartado por falta de espacio desde que se creó la cola.
	std::size_t droppedTotal() const { return droppedTotal_; }

private:
	std::size_t trim()
	{
		std::size_t dropped = 0;
		while (items_.size() > capacity_) {
			items_.pop_front();
			++dropped;
		}
		droppedTotal_ += dropped;
		return dropped;
	}

	std::size_t capacity_;
	std::size_t droppedTotal_ = 0;
	std::deque<T> items_;
};

} // namespace tts
