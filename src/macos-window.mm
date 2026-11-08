/*
Multidestination Studio
Copyright (C) 2026 sebts

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "macos-window.hpp"

#import <AppKit/AppKit.h>

#include <QWidget>

namespace {

    NSWindow *NativeWindow(QWidget *window)
    {
        if (!window)
            return nil;
        NSView *view = (__bridge NSView *) reinterpret_cast<void *>(window->winId());
        return view ? view.window : nil;
    }

}  // namespace

namespace MacOSWindow {

    void MakeOutputWindow(QWidget *window)
    {
        NSWindow *win = NativeWindow(window);
        if (!win)
            return;
        const NSWindowCollectionBehavior behavior =
            NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
            NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorIgnoresCycle;
        NSScreen *screen = win.screen;
        /* Déjà réglée : rien à refaire (évite des allers-retours avec Qt). */
        if (win.collectionBehavior == behavior && win.level == NSScreenSaverWindowLevel &&
            (!screen || NSEqualRects(win.frame, screen.frame)))
            return;
        /* Tous les Spaces, fixe : une app qui passe en plein écran natif (son
	 * propre Space) ou un passage d'une fenêtre d'OBS à l'autre ne fait
	 * plus disparaître ni glisser la sortie. Auxiliaire du plein écran,
	 * jamais plein écran elle-même ; hors du cycle Cmd+`. */
        win.collectionBehavior = behavior;
        /* Sans bordure (Qt::FramelessWindowHint), sans ombre. */
        win.styleMask = NSWindowStyleMaskBorderless;
        win.hasShadow = NO;
        win.hidesOnDeactivate = NO;
        /* Au-dessus de la barre des menus et du Dock, pour couvrir tout l'écran. */
        win.level = NSScreenSaverWindowLevel;
        /* Tout l'écran, barre des menus comprise (Qt s'arrête sous la barre). */
        if (screen)
            [win setFrame:screen.frame display:YES];
    }

    void MakeNormalWindow(QWidget *window)
    {
        NSWindow *win = NativeWindow(window);
        if (!win)
            return;
        win.collectionBehavior = NSWindowCollectionBehaviorDefault;
        win.level = NSNormalWindowLevel;
        win.hasShadow = YES;
    }

}  // namespace MacOSWindow
