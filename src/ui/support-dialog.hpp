#pragma once

// "Invítame un café": un mensaje breve con el enlace a Ko-fi. Solo se abre cuando el usuario lo
// pide (el botón al final de las propiedades de las fuentes o el menú de la rueda del dock);
// nunca aparece solo.

class QWidget;

void open_support_dialog(QWidget *parent);
