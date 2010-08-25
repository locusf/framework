/* * This file is part of meego-im-framework *
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 * All rights reserved.
 * Contact: Nokia Corporation (directui@nokia.com)
 *
 * If you have questions regarding the use of this file, please contact
 * Nokia at directui@nokia.com.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * and appearing in the file LICENSE.LGPL included in the packaging
 * of this file.
 */

#define _BSD_SOURCE             // for mkdtemp

#include "minputcontextglibdbusconnection.h"
#include "mtoolbarmanager.h"
#include "mtoolbarid.h"

#include <QDataStream>
#include <QDebug>
#include <QRegion>
#include <QKeyEvent>
#include <QMap>
#include <QString>
#include <QVariant>

#include <stdlib.h>

#include "minputmethodbase.h"
#include "mimapplication.h"

namespace
{
    const char * const SocketDirectoryTemplate = "/tmp/meego-im-uiserver-XXXXXX";
    const char * const SocketName = "imserver_dbus";
    const char * const DBusPath = "/com/meego/inputmethod/uiserver1";

    const char * const DBusClientPath = "/com/meego/inputmethod/inputcontext";
    const char * const DBusClientInterface = "com.meego.inputmethod.inputcontext1";

    const char * const ActivationBusName("com.meego.inputmethod.uiserver1");
    const char * const ActivationPath("/com/meego/inputmethod/activation");

    // attribute names for updateWidgetInformation() map
    const char * const FocusStateAttribute = "focusState";
    const char * const ContentTypeAttribute = "contentType";
    const char * const CorrectionAttribute = "correctionEnabled";
    const char * const PredictionAttribute = "predictionEnabled";
    const char * const AutoCapitalizationAttribute = "autocapitalizationEnabled";
    const char * const SurroundingTextAttribute = "surroundingText";
    const char * const CursorPositionAttribute = "cursorPosition";
    const char * const HasSelectionAttribute = "hasSelection";
    const char * const InputMethodModeAttribute = "inputMethodMode";
    const char * const VisualizationAttribute = "visualizationPriority";
    const char * const ToolbarIdAttribute = "toolbarId";
    const char * const ToolbarAttribute = "toolbar";
    const char * const WinId = "winId";
}

//! \brief Class for a mostly dummy object for activating input method server over D-Bus
//!
//! The class has only one method, address, which is used to obtain the address of the
//! private D-Bus server socket.
struct MIMSDBusActivater
{
    GObject parent;

    //! Address of our D-Bus server socket, owned by MInputContextGlibDBusConnection
    const char *address;
};

//! \brief MIMSDBusActivater metaclass
struct MIMSDBusActivaterClass
{
    GObjectClass parent;
};

#define M_TYPE_IMS_DBUS_ACTIVATER              (m_ims_dbus_activater_get_type())
#define M_IMS_DBUS_ACTIVATER(object)           (G_TYPE_CHECK_INSTANCE_CAST((object), M_TYPE_IMS_DBUS_ACTIVATER, MIMSDBusActivater))
#define M_IMS_DBUS_ACTIVATER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass), M_TYPE_IMS_DBUS_ACTIVATER, MIMSDBusActivaterClass))
#define M_IS_M_IMS_DBUS_ACTIVATER(object)      (G_TYPE_CHECK_INSTANCE_TYPE((object), M_TYPE_IMS_DBUS_ACTIVATER))
#define M_IS_M_IMS_DBUS_ACTIVATER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), M_TYPE_IMS_DBUS_ACTIVATER))
#define M_IMS_DBUS_ACTIVATER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS((obj), M_TYPE_IMS_DBUS_ACTIVATER, MIMSDBusActivaterClass))

G_DEFINE_TYPE(MIMSDBusActivater, m_ims_dbus_activater, G_TYPE_OBJECT)

static gboolean
m_ims_dbus_activater_address(MIMSDBusActivater *obj, char **address, GError **/*error*/)
{
    *address = g_strdup(obj->address);
    return TRUE;
}


#include "mimsdbusactivaterserviceglue.h"

static void
m_ims_dbus_activater_init(MIMSDBusActivater */*obj*/)
{
}

static void
m_ims_dbus_activater_class_init(MIMSDBusActivaterClass */*klass*/)
{
    dbus_g_object_type_install_info(M_TYPE_IMS_DBUS_ACTIVATER,
                                    &dbus_glib_m_ims_dbus_activater_object_info);
}


//! \brief GObject-based input context connection class
//!
//! There is one of these for each client connection
struct MDBusGlibICConnection
{
    GObject parent;

    //! This is the glib-level connection established when the input context calls us
    DBusGConnection* dbusConnection;
    //! This is a proxy object used to call input context methods
    DBusGProxy *inputContextProxy;
    //! The actual C++-level connection that acts as a proxy for the currently active input context
    MInputContextGlibDBusConnection *icConnection;
    //! Running number used to identify the connection, used for toolbar ids
    unsigned int connectionNumber;
};

struct MDBusGlibICConnectionClass
{
    GObjectClass parent;
};

#define M_TYPE_DBUS_GLIB_IC_CONNECTION              (m_dbus_glib_ic_connection_get_type())
#define M_DBUS_GLIB_IC_CONNECTION(object)           (G_TYPE_CHECK_INSTANCE_CAST((object), M_TYPE_DBUS_GLIB_IC_CONNECTION, MDBusGlibICConnection))
#define M_DBUS_GLIB_IC_CONNECTION_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass), M_TYPE_DBUS_GLIB_IC_CONNECTION, MDBusGlibICConnectionClass))
#define M_IS_M_DBUS_GLIB_IC_CONNECTION(object)      (G_TYPE_CHECK_INSTANCE_TYPE((object), M_TYPE_DBUS_GLIB_IC_CONNECTION))
#define M_IS_M_DBUS_GLIB_IC_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), M_TYPE_DBUS_GLIB_IC_CONNECTION))
#define M_DBUS_GLIB_IC_CONNECTION_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS((obj), M_TYPE_DBUS_GLIB_IC_CONNECTION, MDBusGlibICConnectionClass))

G_DEFINE_TYPE(MDBusGlibICConnection, m_dbus_glib_ic_connection, G_TYPE_OBJECT)

static gboolean
m_dbus_glib_ic_connection_activate_context(MDBusGlibICConnection *obj, GError **/*error*/)
{
    obj->icConnection->activateContext(obj);
    return TRUE;
}

static gboolean
m_dbus_glib_ic_connection_show_input_method(MDBusGlibICConnection *obj, GError **/*error*/)
{
    obj->icConnection->showInputMethod();
    return TRUE;
}

static gboolean
m_dbus_glib_ic_connection_hide_input_method(MDBusGlibICConnection *obj, GError **/*error*/)
{
    obj->icConnection->hideInputMethod();
    return TRUE;
}

static gboolean
m_dbus_glib_ic_connection_mouse_clicked_on_preedit(MDBusGlibICConnection *obj,
                                                   gint32 posX, gint32 posY,
                                                   gint32 preeditX, gint32 preeditY,
                                                   gint32 preeditWidth, gint32 preeditHeight,
                                                   GError **/*error*/)
{
    obj->icConnection->mouseClickedOnPreedit(QPoint(posX, posY), QRect(preeditX, preeditY,
                                                                       preeditWidth, preeditHeight));
    return TRUE;
}

static gboolean
m_dbus_glib_ic_connection_set_preedit(MDBusGlibICConnection *obj, const char *text,
                                      GError **/*error*/)
{
    obj->icConnection->setPreedit(QString::fromUtf8(text));
    return TRUE;
}

static bool deserializeVariant(QVariant& target, const GArray *data, const char *methodName)
{
    const QByteArray storageWrapper(QByteArray::fromRawData(data->data, data->len));
    QDataStream dataStream(storageWrapper);
    dataStream >> target;
    if (dataStream.status() != QDataStream::Ok || !target.isValid()) {
        qWarning("m_dbus_glib_ic_connection: Invalid parameter to %s.", methodName);
        return false;
    }
    return true;
}

template <typename T>
static bool deserializeValue(T& target, const GArray *data, const char *methodName)
{
    QVariant variant;
    if (!deserializeVariant(variant, data, methodName)) {
        return false;
    }
    if (!variant.canConvert<T>()) {
        qWarning("m_dbus_glib_ic_connection: Invalid parameter to %s.", methodName);
        return false;
    }
    target = variant.value<T>();
    return true;
}

static gboolean
m_dbus_glib_ic_connection_update_widget_information(MDBusGlibICConnection *obj,
                                                    GArray *stateInformation,
                                                    gboolean focusChanged, GError **/*error*/)
{
    QMap<QString, QVariant> stateMap;
    if (deserializeValue(stateMap, stateInformation, "updateWidgetInformation")) {
        obj->icConnection->updateWidgetInformation(obj, stateMap, focusChanged == TRUE);
    }
    return TRUE;
}

static gboolean
m_dbus_glib_ic_connection_reset(MDBusGlibICConnection *obj, GError **/*error*/)
{
    obj->icConnection->reset();
    return TRUE;
}

static gboolean
m_dbus_glib_ic_connection_app_orientation_changed(MDBusGlibICConnection *obj, gint32 angle,
                                                  GError **/*error*/)
{
    obj->icConnection->appOrientationChanged(static_cast<int>(angle));
    return TRUE;
}

static gboolean
m_dbus_glib_ic_connection_set_copy_paste_state(MDBusGlibICConnection *obj, gboolean copyAvailable,
                                               gboolean pasteAvailable, GError **/*error*/)
{
    obj->icConnection->setCopyPasteState(copyAvailable == TRUE, pasteAvailable == TRUE);
    return TRUE;
}

static gboolean
m_dbus_glib_ic_connection_process_key_event(MDBusGlibICConnection *obj, gint32 keyType,
                                            gint32 keyCode, gint32 modifiers,
                                            const char *text, gboolean autoRepeat, gint32 count,
                                            guint32 nativeScanCode, guint32 nativeModifiers,
                                            GError **/*error*/)
{
    obj->icConnection->processKeyEvent(static_cast<QEvent::Type>(keyType),
                                       static_cast<Qt::Key>(keyCode),
                                       static_cast<Qt::KeyboardModifiers>(modifiers),
                                       QString::fromUtf8(text), autoRepeat == TRUE,
                                       static_cast<int>(count), static_cast<quint32>(nativeScanCode),
                                       static_cast<quint32>(nativeModifiers));
    return TRUE;
}

static gboolean
m_dbus_glib_ic_connection_register_toolbar(MDBusGlibICConnection *obj, gint32 id,
                                           const char *fileName, GError **/*error*/)
{
    obj->icConnection->registerToolbar(obj, static_cast<int>(id), QString::fromUtf8(fileName));
    return TRUE;
}

static gboolean
m_dbus_glib_ic_connection_unregister_toolbar(MDBusGlibICConnection *obj, gint32 id,
                                             GError **/*error*/)
{
    obj->icConnection->unregisterToolbar(obj, static_cast<int>(id));
    return TRUE;
}

static gboolean
m_dbus_glib_ic_connection_set_toolbar_item_attribute(MDBusGlibICConnection *obj, gint32 id,
                                                     const char *item, const char *attribute,
                                                     GArray *value, GError **/*error*/)
{
    QVariant deserializedValue;
    if (deserializeVariant(deserializedValue, value, "setToolbarItemAttribute")) {
        obj->icConnection->setToolbarItemAttribute(obj, static_cast<int>(id), QString::fromUtf8(item),
                                                   QString::fromUtf8(attribute), deserializedValue);
    }
    return TRUE;
}


#include "mdbusglibicconnectionserviceglue.h"

static void
m_dbus_glib_ic_connection_dispose(GObject *object)
{
    qDebug() << __PRETTY_FUNCTION__;

    MDBusGlibICConnection *self(M_DBUS_GLIB_IC_CONNECTION(object));

    if (self->dbusConnection) {
        dbus_g_connection_unref(self->dbusConnection);
        self->dbusConnection = 0;
    }

    G_OBJECT_CLASS(m_dbus_glib_ic_connection_parent_class)->finalize(object);
}


static void
m_dbus_glib_ic_connection_finalize(GObject */*object*/)
{
    qDebug() << __PRETTY_FUNCTION__;
}


static void
m_dbus_glib_ic_connection_init(MDBusGlibICConnection */*obj*/)
{
}

static void
m_dbus_glib_ic_connection_class_init(MDBusGlibICConnectionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = m_dbus_glib_ic_connection_dispose;
    gobject_class->finalize = m_dbus_glib_ic_connection_finalize;

    dbus_g_object_type_install_info(M_TYPE_DBUS_GLIB_IC_CONNECTION,
                                    &dbus_glib_m_dbus_glib_ic_connection_object_info);
}


// MInputContextGlibDBusConnection...........................................

static void handleDisconnectionTrampoline(DBusGProxy */*proxy*/, gpointer userData)
{
    qDebug() << __PRETTY_FUNCTION__;

    MDBusGlibICConnection *connection(M_DBUS_GLIB_IC_CONNECTION(userData));
    connection->icConnection->handleDBusDisconnection(connection);
}

void MInputContextGlibDBusConnection::handleDBusDisconnection(MDBusGlibICConnection *connection)
{
    g_object_unref(G_OBJECT(connection));

    if (activeContext != connection) {
        return;
    }

    activeContext = 0;

    // notify plugins
    foreach (MInputMethodBase *target, targets()) {
        target->clientChanged();
    }
}

static void handleNewConnection(DBusServer */*server*/, DBusConnection *connection, gpointer userData)
{
    qDebug() << __PRETTY_FUNCTION__;
    dbus_connection_ref(connection);
    dbus_connection_setup_with_g_main(connection, NULL);

    MDBusGlibICConnection *obj = M_DBUS_GLIB_IC_CONNECTION(
        g_object_new(M_TYPE_DBUS_GLIB_IC_CONNECTION, NULL));

    obj->dbusConnection = dbus_connection_get_g_connection(connection);
    obj->icConnection = static_cast<MInputContextGlibDBusConnection *>(userData);

    // Proxy for calling input context methods
    DBusGProxy *inputContextProxy = dbus_g_proxy_new_for_peer(
        obj->dbusConnection, DBusClientPath, DBusClientInterface);
    if (!inputContextProxy) {
        qFatal("Unable to find the service.");
    }
    obj->inputContextProxy = inputContextProxy;

    g_signal_connect(G_OBJECT(inputContextProxy), "destroy",
                     G_CALLBACK(handleDisconnectionTrampoline), obj);

    static unsigned int connectionCounter = 0;
    obj->connectionNumber = connectionCounter++;

    dbus_g_connection_register_g_object(obj->dbusConnection, DBusPath, G_OBJECT(obj));
}


MInputContextGlibDBusConnection::MInputContextGlibDBusConnection()
    : activeContext(NULL),
      server(NULL),
      sessionBusConnection(NULL),
      activater(NULL)
{
    dbus_g_thread_init();
    g_type_init();

    socketAddress = SocketDirectoryTemplate;
    if (!mkdtemp(socketAddress.data())) {
        qFatal("IMServer: couldn't create directory for D-Bus socket.");
    }
    socketAddress.append("/");
    socketAddress.append(SocketName);
    socketAddress.prepend("unix:path=");

    DBusError error;
    dbus_error_init(&error);

    server = dbus_server_listen(socketAddress, &error);
    if (!server) {
        qFatal("Couldn't create D-Bus server: %s", error.message);
    }

    dbus_server_setup_with_g_main(server, NULL);
    dbus_server_set_new_connection_function(server, handleNewConnection, this, NULL);

    // Setup service for automatic activation

    GError *gerror = NULL;

    sessionBusConnection = dbus_g_bus_get(DBUS_BUS_SESSION, &gerror);
    if (!sessionBusConnection) {
        qWarning("IMServer: unable to create session D-Bus connection: %s", gerror->message);
        return;
    }


    DBusGProxy *busProxy(dbus_g_proxy_new_for_name(sessionBusConnection, "org.freedesktop.DBus",
                                                   "/org/freedesktop/DBus",
                                                   "org.freedesktop.DBus"));
    guint nameRequestResult;
    if (!dbus_g_proxy_call(busProxy, "RequestName", &gerror,
                           G_TYPE_STRING, ActivationBusName,
                           G_TYPE_UINT, 0,
                           G_TYPE_INVALID,
                           G_TYPE_UINT, &nameRequestResult,
                           G_TYPE_INVALID)) {
        qWarning("IMServer: failed to acquire activation service name: %s", gerror->message);
    }

    activater = M_IMS_DBUS_ACTIVATER(g_object_new(M_TYPE_IMS_DBUS_ACTIVATER, NULL));
    activater->address = socketAddress.constData();

    dbus_g_connection_register_g_object(sessionBusConnection, ActivationPath, G_OBJECT(activater));
}


MInputContextGlibDBusConnection::~MInputContextGlibDBusConnection()
{
    dbus_g_connection_unregister_g_object(sessionBusConnection, G_OBJECT(activater));
    g_object_unref(G_OBJECT(activater));

    dbus_g_connection_unref(sessionBusConnection);
    dbus_server_disconnect(server);
    dbus_server_unref(server);
}


// Server -> input context...................................................

void MInputContextGlibDBusConnection::sendPreeditString(const QString &string,
                                                       PreeditFace preeditFace)
{
    if (activeContext) {
        dbus_g_proxy_call_no_reply(activeContext->inputContextProxy, "updatePreedit",
                                   G_TYPE_STRING, string.toUtf8().data(),
                                   G_TYPE_UINT, static_cast<unsigned int>(preeditFace),
                                   G_TYPE_INVALID);
    }
}


void MInputContextGlibDBusConnection::sendCommitString(const QString &string)
{
    if (activeContext) {
        dbus_g_proxy_call_no_reply(activeContext->inputContextProxy, "commitString",
                                   G_TYPE_STRING, string.toUtf8().data(),
                                   G_TYPE_INVALID);
    }
}


void MInputContextGlibDBusConnection::sendKeyEvent(const QKeyEvent &keyEvent)
{
    if (activeContext) {
        int type = static_cast<int>(keyEvent.type());
        int key = static_cast<int>(keyEvent.key());
        int modifiers = static_cast<int>(keyEvent.modifiers());

        dbus_g_proxy_call_no_reply(activeContext->inputContextProxy, "keyEvent",
                                   G_TYPE_INT, type,
                                   G_TYPE_INT, key,
                                   G_TYPE_INT, modifiers,
                                   G_TYPE_STRING, keyEvent.text().toUtf8().data(),
                                   G_TYPE_BOOLEAN, keyEvent.isAutoRepeat() ? TRUE : FALSE,
                                   G_TYPE_INT, keyEvent.count(),
                                   G_TYPE_INVALID);
    }
}


void MInputContextGlibDBusConnection::notifyImInitiatedHiding()
{
    if (activeContext) {
        dbus_g_proxy_call_no_reply(activeContext->inputContextProxy, "imInitiatedHide",
                                   G_TYPE_INVALID);
    }
}


void MInputContextGlibDBusConnection::setGlobalCorrectionEnabled(bool enabled)
{
    if ((enabled != globalCorrectionEnabled) && activeContext) {
        dbus_g_proxy_call_no_reply(activeContext->inputContextProxy, "setGlobalCorrectionEnabled",
                                   G_TYPE_BOOLEAN, enabled ? TRUE : FALSE,
                                   G_TYPE_INVALID);
    }

    globalCorrectionEnabled = enabled;
}


QRect MInputContextGlibDBusConnection::preeditRectangle(bool &valid)
{
    GError *error = NULL;

    gboolean gvalidity;
    gint32 x, y, width, height;

    if (!dbus_g_proxy_call(activeContext->inputContextProxy, "preeditRectangle", &error, G_TYPE_INVALID,
                           G_TYPE_BOOLEAN, &gvalidity, G_TYPE_INT, &x, G_TYPE_INT, &y,
                           G_TYPE_INT, &width, G_TYPE_INT, &height, G_TYPE_INVALID)) {
        g_error_free(error);
        valid = false;
        return QRect();
    }
    valid = gvalidity == TRUE;
    return QRect(x, y, width, height);
}

void MInputContextGlibDBusConnection::setRedirectKeys(bool enabled)
{
    if ((redirectionEnabled != enabled) && activeContext) {
        dbus_g_proxy_call_no_reply(activeContext->inputContextProxy, "setRedirectKeys",
                                   G_TYPE_BOOLEAN, enabled ? TRUE : FALSE,
                                   G_TYPE_INVALID);
    }
    redirectionEnabled = enabled;
}

void MInputContextGlibDBusConnection::setDetectableAutoRepeat(bool enabled)
{
    if ((detectableAutoRepeat != enabled) && activeContext) {
        dbus_g_proxy_call_no_reply(activeContext->inputContextProxy, "setDetectableAutoRepeat",
                                   G_TYPE_BOOLEAN, enabled ? TRUE : FALSE,
                                   G_TYPE_INVALID);
    }
    detectableAutoRepeat = enabled;
}


void MInputContextGlibDBusConnection::copy()
{
    dbus_g_proxy_call_no_reply(activeContext->inputContextProxy, "copy",
                               G_TYPE_INVALID);
}


void MInputContextGlibDBusConnection::paste()
{
    dbus_g_proxy_call_no_reply(activeContext->inputContextProxy, "paste",
                               G_TYPE_INVALID);
}


void MInputContextGlibDBusConnection::updateInputMethodArea(const QRegion &region)
{
    if (activeContext) {
        QList<QVariant> data;
        data.append(region.boundingRect());

        QByteArray temporaryStorage;
        QDataStream valueStream(&temporaryStorage, QIODevice::WriteOnly);
        valueStream << data;
        GArray * const gdata(g_array_sized_new(FALSE, FALSE, 1, temporaryStorage.size()));
        g_array_append_vals(gdata, temporaryStorage.constData(),
                            temporaryStorage.size());

        dbus_g_proxy_call_no_reply(activeContext->inputContextProxy, "updateInputMethodArea",
                                   DBUS_TYPE_G_UCHAR_ARRAY, gdata,
                                   G_TYPE_INVALID);

        g_array_unref(gdata);
    }
}


// Input context -> server...................................................

void MInputContextGlibDBusConnection::activateContext(MDBusGlibICConnection *connection)
{
    MDBusGlibICConnection *previousActive = activeContext;

    activeContext = connection;

    if (activeContext) {
        dbus_g_proxy_call_no_reply(activeContext->inputContextProxy, "setGlobalCorrectionEnabled",
                                   G_TYPE_BOOLEAN, globalCorrectionEnabled,
                                   G_TYPE_INVALID);
        dbus_g_proxy_call_no_reply(activeContext->inputContextProxy, "setRedirectKeys",
                                   G_TYPE_BOOLEAN, redirectionEnabled,
                                   G_TYPE_INVALID);
        dbus_g_proxy_call_no_reply(activeContext->inputContextProxy, "detectableAutoRepeat",
                                   G_TYPE_BOOLEAN, detectableAutoRepeat,
                                   G_TYPE_INVALID);

        if ((previousActive != 0) && (previousActive != activeContext)) {
            // TODO: we can't use previousActive here like this
            dbus_g_proxy_call_no_reply(previousActive->inputContextProxy, "activationLostEvent",
                                       G_TYPE_INVALID);
        }
    }

    // notify plugins
    foreach (MInputMethodBase *target, targets()) {
        target->clientChanged();
    }
}


void MInputContextGlibDBusConnection::showInputMethod()
{
    emit showInputMethodRequest();
}


void MInputContextGlibDBusConnection::hideInputMethod()
{
    emit hideInputMethodRequest();
}


void MInputContextGlibDBusConnection::mouseClickedOnPreedit(const QPoint &pos,
                                                           const QRect &preeditRect)
{
    foreach (MInputMethodBase *target, targets()) {
        target->mouseClickedOnPreedit(pos, preeditRect);
    }
}


void MInputContextGlibDBusConnection::setPreedit(const QString &text)
{
    foreach (MInputMethodBase *target, targets()) {
        target->setPreedit(text);
    }
}


void MInputContextGlibDBusConnection::reset()
{
    foreach (MInputMethodBase *target, targets()) {
        target->reset();
    }
}


bool MInputContextGlibDBusConnection::surroundingText(QString &text, int &cursorPosition)
{
    QVariant textVariant = widgetState[SurroundingTextAttribute];
    QVariant posVariant = widgetState[CursorPositionAttribute];

    if (textVariant.isValid() && posVariant.isValid()) {
        text = textVariant.toString();
        cursorPosition = posVariant.toInt();
        return true;
    }

    return false;
}


bool MInputContextGlibDBusConnection::hasSelection(bool &valid)
{
    QVariant selectionVariant = widgetState[HasSelectionAttribute];
    valid = selectionVariant.isValid();
    return selectionVariant.toBool();
}


int MInputContextGlibDBusConnection::inputMethodMode(bool &valid)
{
    QVariant modeVariant = widgetState[InputMethodModeAttribute];
    return modeVariant.toInt(&valid);
}

int MInputContextGlibDBusConnection::winId(bool &valid)
{
    QVariant winIdVariant = widgetState[WinId];
    return winIdVariant.toInt(&valid);
}
int MInputContextGlibDBusConnection::contentType(bool &valid)
{
    QVariant contentTypeVariant = widgetState[ContentTypeAttribute];
    return contentTypeVariant.toInt(&valid);
}


bool MInputContextGlibDBusConnection::correctionEnabled(bool &valid)
{
    QVariant correctionVariant = widgetState[CorrectionAttribute];
    valid = correctionVariant.isValid();
    return correctionVariant.toBool();
}


bool MInputContextGlibDBusConnection::predictionEnabled(bool &valid)
{
    QVariant predictionVariant = widgetState[PredictionAttribute];
    valid = predictionVariant.isValid();
    return predictionVariant.toBool();
}


bool MInputContextGlibDBusConnection::autoCapitalizationEnabled(bool &valid)
{
    QVariant capitalizationVariant = widgetState[AutoCapitalizationAttribute];
    valid = capitalizationVariant.isValid();
    return capitalizationVariant.toBool();
}


void
MInputContextGlibDBusConnection::updateWidgetInformation(
    MDBusGlibICConnection *connection, const QMap<QString, QVariant> &stateInfo,
    bool focusChanged)
{
    // check visualization change
    bool oldVisualization = false;
    bool newVisualization = false;

    QVariant variant = widgetState[VisualizationAttribute];

    if (variant.isValid()) {
        oldVisualization = variant.toBool();
    }

    variant = stateInfo[VisualizationAttribute];
    if (variant.isValid()) {
        newVisualization = variant.toBool();
    }

    // toolbar change
    MToolbarId oldToolbarId;
    MToolbarId newToolbarId;
    oldToolbarId = toolbarId;

    variant = stateInfo[ToolbarIdAttribute];
    if (variant.isValid()) {
        // map toolbar id from local to global
        newToolbarId = MToolbarId(variant.toInt(), QString::number(connection->connectionNumber));
    }

    // update state
    widgetState = stateInfo;

    if (focusChanged) {
        foreach (MInputMethodBase *target, targets()) {
            target->focusChanged(stateInfo[FocusStateAttribute].toBool());
        }

        updateTransientHint();
    }

    // call notification methods if needed
    if (oldVisualization != newVisualization) {
        foreach (MInputMethodBase *target, targets()) {
            target->visualizationPriorityChanged(newVisualization);
        }
    }

    // compare the toolbar id (global)
    if (oldToolbarId != newToolbarId) {
        QString toolbarFile = stateInfo[ToolbarAttribute].toString();
        if (!MToolbarManager::instance().contains(newToolbarId) && !toolbarFile.isEmpty()) {
            // register toolbar if toolbar manager does not contain it but
            // toolbar file is not empty. This can reload the toolbar data
            // if im-uiserver crashes.
            variant = stateInfo[ToolbarIdAttribute];
            if (variant.isValid()) {
                const int toolbarLocalId = variant.toInt();
                registerToolbar(connection, toolbarLocalId, toolbarFile);
            }
        }
        QSharedPointer<const MToolbarData> toolbar =
            MToolbarManager::instance().toolbarData(newToolbarId);

        foreach (MInputMethodBase *target, targets()) {
            target->setToolbar(toolbar);
        }
        // store the new used toolbar id(global).
        toolbarId = newToolbarId;
    }

    // general notification last
    foreach (MInputMethodBase *target, targets()) {
        target->update();
    }
}


void MInputContextGlibDBusConnection::appOrientationChanged(int angle)
{
    foreach (MInputMethodBase *target, targets()) {
        target->appOrientationChanged(angle);
    }
}


void MInputContextGlibDBusConnection::setCopyPasteState(bool copyAvailable, bool pasteAvailable)
{
    foreach (MInputMethodBase *target, targets()) {
        target->setCopyPasteState(copyAvailable, pasteAvailable);
    }
}


void MInputContextGlibDBusConnection::processKeyEvent(
    QEvent::Type keyType, Qt::Key keyCode, Qt::KeyboardModifiers modifiers, const QString &text,
    bool autoRepeat, int count, quint32 nativeScanCode, quint32 nativeModifiers)
{
    foreach (MInputMethodBase *target, targets()) {
        target->processKeyEvent(keyType, keyCode, modifiers, text, autoRepeat, count,
                                nativeScanCode, nativeModifiers);
    }
}

void MInputContextGlibDBusConnection::registerToolbar(MDBusGlibICConnection *connection, int id,
                                                     const QString &toolbar)
{
    MToolbarId globalId(id, QString::number(connection->connectionNumber));
    if (globalId.isValid() && !toolbarIds.contains(globalId)) {
        MToolbarManager::instance().registerToolbar(globalId, toolbar);
        toolbarIds.insert(globalId);
    }
}

void MInputContextGlibDBusConnection::unregisterToolbar(MDBusGlibICConnection *connection, int id)
{
    MToolbarId globalId(id, QString::number(connection->connectionNumber));
    if (globalId.isValid() && toolbarIds.contains(globalId)) {
        MToolbarManager::instance().unregisterToolbar(globalId);
        toolbarIds.remove(globalId);
    }
}

void MInputContextGlibDBusConnection::setToolbarItemAttribute(
    MDBusGlibICConnection *connection, int id, const QString &item, const QString &attribute,
    const QVariant &value)
{
    MToolbarId globalId(id, QString::number(connection->connectionNumber));
    if (globalId.isValid() && toolbarIds.contains(globalId)) {
        MToolbarManager::instance().setToolbarItemAttribute(globalId, item, attribute, value);
    }
}

void MInputContextGlibDBusConnection::updateTransientHint()
{
    bool ok = false;
    const int appWinId = winId(ok);

    if (ok) {
        MIMApplication *app = MIMApplication::instance();

        if (app) {
            app->setTransientHint(appWinId);
        }
    }
}