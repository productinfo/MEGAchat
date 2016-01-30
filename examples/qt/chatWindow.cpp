#include "chatWindow.h"
#include "mainwindow.h"

ChatWindow::ChatWindow(karere::ChatRoom& room, MainWindow& parent): QDialog(&parent),
    mainWindow(parent), mRoom(room), mWaitMsg(*this)
{
    ui.setupUi(this);
    ui.mSplitter->setStretchFactor(0,1);
    ui.mSplitter->setStretchFactor(1,0);
    ui.mMessageList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui.mMsgSendBtn, SIGNAL(clicked()), this, SLOT(onMsgSendBtn()));
    connect(ui.mMessageEdit, SIGNAL(sendMsg()), this, SLOT(onMsgSendBtn()));
    connect(ui.mMessageEdit, SIGNAL(editLastMsg()), this, SLOT(editLastMsg()));
    connect(ui.mMessageList, SIGNAL(requestHistory()), this, SLOT(onMsgListRequestHistory()));
    connect(ui.mVideoCallBtn, SIGNAL(clicked(bool)), this, SLOT(onVideoCallBtn(bool)));
    connect(ui.mAudioCallBtn, SIGNAL(clicked(bool)), this, SLOT(onAudioCallBtn(bool)));
    connect(ui.mMembersBtn, SIGNAL(clicked(bool)), this, SLOT(onMembersBtn(bool)));
    connect(ui.mMessageList->verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(onScroll(int)));
    ui.mAudioCallBtn->hide();
    ui.mVideoCallBtn->hide();
    ui.mChatdStatusDisplay->hide();
    if (!mRoom.isGroup())
        ui.mMembersBtn->hide();
    else
        setAcceptDrops(true);
    QDialog::show();
}

ChatWindow::~ChatWindow()
{
    mMessages->setListener(static_cast<chatd::Listener*>(&mRoom));
}
MessageWidget::MessageWidget(ChatWindow& parent, const chatd::Message& msg,
    chatd::Message::Status status, chatd::Idx idx)
: QWidget(&parent), mChatWindow(parent), mMessage(&msg), mOriginal(msg),
    mIsMine(msg.userid == parent.messages().client().userId()), mIndex(idx)
{
    ui.setupUi(this);
    setAuthor(msg.userid);
    setTimestamp(msg.ts);
    setStatus(status);
    setText(msg);
    show();
}

void ChatWindow::createMembersMenu(QMenu& menu)
{
    assert(mRoom.isGroup());
    auto& room = static_cast<karere::GroupChatRoom&>(mRoom);
    if (room.peers().empty())
    {
        menu.addAction(tr("You are alone in this chatroom"))->setEnabled(false);
        return;
    }
    for (auto& item: room.peers())
    {
        auto entry = menu.addMenu(QString::fromStdString(item.second->name()));
        if (room.ownPriv() == chatd::PRIV_OPER)
        {
            auto actRemove = entry->addAction(tr("Remove from chat"));
            actRemove->setData(QVariant(item.first));
            auto actSetPriv = entry->addAction(tr("Set privilege"));
            actSetPriv->setData(QVariant(item.first));
            auto actPrivChat = entry->addAction(tr("Send private message"));
            actPrivChat->setData(QVariant(item.first));
            connect(actRemove, SIGNAL(triggered()), SLOT(onMemberRemove()));
            connect(actSetPriv, SIGNAL(triggered()), SLOT(onMemberSetPriv()));
            connect(actPrivChat, SIGNAL(triggered()), SLOT(onMemberPrivateChat()));
        }
    }
}
uint64_t handleFromAction(QObject* object)
{
    if (!object)
        throw std::runtime_error("handleFromAction: NULL object provided");
    auto action = qobject_cast<QAction*>(object);
    bool ok;
    uint64_t userid = action->data().toULongLong(&ok);
    assert(ok);
    return userid;
}

void ChatWindow::onMemberRemove()
{
    uint64_t handle(handleFromAction(QObject::sender()));
    mWaitMsg.addMsg(tr("Removing user(s), please wait..."));
    auto waitMsgKeepalive = mWaitMsg;
    mainWindow.client().api->call(&mega::MegaApi::removeFromChat, mRoom.chatid(), handle)
    .fail([this, waitMsgKeepalive](const promise::Error& err)
    {
        QMessageBox::critical(nullptr, tr("Remove member from group chat"),
            tr("Error removing member from group chat: %1")
            .arg(QString::fromStdString(err.msg())));
        return err;
    });
}
void ChatWindow::onMemberSetPriv()
{
//    static_cast<karere::GroupChatRoom&>(mRoom).setPriv(handleFromAction(QObject::sender()));
    QMessageBox::critical(this, tr("Set member privilege"), tr("Not implemented yet"));
}
void ChatWindow::onMemberPrivateChat()
{
    auto& clist = *mainWindow.client().contactList;
    auto it = clist.find(handleFromAction(QObject::sender()));
    if (it == clist.end())
    {
        QMessageBox::critical(this, tr("Send private message"), tr("Person is not a contact of ours"));
        return;
    }
    it->second->gui().showChatWindow();
}
void ChatWindow::onMembersBtn(bool)
{
    mega::marshallCall([this]()
    {
        QMenu menu(this);
        createMembersMenu(menu);
        menu.setLayoutDirection(Qt::RightToLeft);
        menu.adjustSize();
        menu.exec(ui.mMembersBtn->mapToGlobal(
            QPoint(-menu.width()+ui.mMembersBtn->width(), ui.mMembersBtn->height())));
    });
}
void ChatWindow::dropEvent(QDropEvent* event)
{
    const auto& data = event->mimeData()->data("application/mega-user-handle");
    if (data.size() != sizeof(uint64_t))
    {
        KR_LOG_ERROR("ChatWindow: dropEvent() for userid: Data size is not 8 bytes");
        return;
    }
    mWaitMsg.addMsg(tr("Adding user(s), please wait..."));
    auto waitMsgKeepAlive = mWaitMsg;
    mRoom.parent.client.api->call(&::mega::MegaApi::inviteToChat, mRoom.chatid(), *(const uint64_t*)(data.data()), chatd::PRIV_FULL)
    .fail([waitMsgKeepAlive](const promise::Error& err)
    {
        QMessageBox::critical(nullptr, tr("Add user"), tr("Error adding user to group chat: ")+QString::fromStdString(err.msg()));
        return err;
    });
    event->acceptProposedAction();
}

void ChatWindow::onScroll(int value)
{
    if (isVisible())
        updateSeen();
}

void ChatWindow::updateSeen()
{
    auto& msglist = *ui.mMessageList;
    if (msglist.count() < 1)
        return;
    int i = msglist.indexAt(QPoint(4, 1)).row();
    if (i < 0) //message list is empty
    {
        printf("no visible messages\n");
        return;
    }

    auto lastidx = mMessages->lastSeenIdx();
    auto rect = msglist.rect();
    chatd::Idx idx = CHATD_IDX_INVALID;
    //find last visible message widget
    for(; i < msglist.count(); i++)
    {
        auto item = msglist.item(i);
        if (msglist.visualItemRect(item).bottom() > rect.bottom())
            break;
        auto lastWidget = qobject_cast<MessageWidget*>(msglist.itemWidget(item));
        if (lastWidget->mIsMine)
            continue;
        auto currIdx = lastWidget->mIndex;
        if (currIdx == CHATD_IDX_INVALID)
            break;
        idx = currIdx;
    }
    if (idx == CHATD_IDX_INVALID)
        return;
    else if (idx > lastidx)
    {
        mMessages->setMessageSeen(idx);
//        printf("set last seen: +%d\n", idx-lastidx);
    }
    else
    {
//        printf("NOT set last seen: %d\n", idx-lastidx);
    }
}

WaitMessage::WaitMessage(ChatWindow& chatWindow)
    :mChatWindow(chatWindow){}
void WaitMessage::addMsg(const QString &msg)
{
    if (!get())
        reset(new WaitMsgWidget(mChatWindow.ui.mMessageList, msg));
    else
        get()->addMsg(msg);
}
WaitMessage::~WaitMessage()
{
    if (use_count() == 2)
        mChatWindow.mWaitMsg.reset();
}

WaitMsgWidget::WaitMsgWidget(QWidget* parent, const QString& msg)
    :QLabel(parent)
{
    setStyleSheet(
        "background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1,"
        "stop:0 rgba(100, 100, 100, 180), stop:1 rgba(120, 120, 120, 180));"
        "border-radius: 10px; font: 16px Arial;"
        "color: white; padding: 10px");
    addMsg(msg);
}

void WaitMsgWidget::addMsg(const QString& msg)
{
    if (!mMsgs.insert(msg).second)
        return;
    hide();
    updateGui();
    show();
}

void WaitMsgWidget::updateGui()
{
    QString text;
    for (auto& msg: mMsgs)
    {
        text.append(msg).append(QChar('\n'));
    }
    if (text.size() > 0)
        text.truncate(text.size()-1);
    setText(text);
    adjustSize();
}

void WaitMsgWidget::show()
{
    move((qobject_cast<QWidget*>(parent())->width()-width())/2, 10);
    QWidget::show();
}