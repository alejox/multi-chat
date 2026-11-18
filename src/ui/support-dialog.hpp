#pragma once

// "Invítame un café": un mensaje breve con el enlace a Ko-fi. Solo se abre cuando el usuario lo
// pide (el enlace al pie del dock o el menú de la rueda); nunca aparece solo.

class QWidget;

void open_support_dialog(QWidget *parent);
