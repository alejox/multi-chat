#pragma once

// Ventana flotante: lo que dibuja una fuente Multichat, en una ventana aparte, transparente y
// siempre encima, para verlo mientras se juega con un solo monitor. La fuente se pinta en una
// textura en el hilo de gráficos (unas 30 veces por segundo) y la ventana muestra esa imagen,
// así que tiene los mismos estilos y opciones que la fuente.
//
// Ajustes de la fuente: float_window (abrirla), float_clickthrough (los clics la atraviesan),
// float_opacity (%) y float_x/y/w/h (dónde quedó; los guarda la ventana).

#include <obs.h>

#include <memory>

namespace overlay_source {
struct OverlaySource;
}

namespace floating {

struct State;

// chat: la fuente de chat puede elegir una disposición propia para la ventana.
void addProperties(obs_properties_t *props, bool chat = false);
void defaults(obs_data_t *settings);
// Abre, cierra o ajusta la ventana según los ajustes. Desde cualquier hilo.
void apply(overlay_source::OverlaySource *s, obs_data_t *settings);
// La fuente se destruye: deja de pintar y cierra la ventana. Desde cualquier hilo.
void close(overlay_source::OverlaySource *s);
// Atajo: cambia "los clics atraviesan" en todas las ventanas abiertas.
void toggleClickThroughAll();

} // namespace floating
