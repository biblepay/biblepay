// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2019 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/biblepay-config.h>
#endif

#include <qt/splashscreen.h>

#include <qt/guiutil.h>
#include <qt/networkstyle.h>

#include <chainparams.h>
#include <clientversion.h>
#include <init.h>
#include <util.h>
#include <ui_interface.h>
#include <version.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif
#include <QTimer>

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QPainter>



SplashScreen::SplashScreen(Qt::WindowFlags f, const NetworkStyle *networkStyle) :
    QWidget(0, f), curAlignment(0)
{

	bool fProd = true;
	if ((gArgs.GetBoolArg("-regtest", false)) || (gArgs.GetBoolArg("-testnet", false)) || (gArgs.IsArgSet("-devnet")))
	{
		fProd = false;
	}

    // transparent background
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet("background:transparent;");

    // no window decorations
    setWindowFlags(Qt::FramelessWindowHint);

    // Geometries of splashscreen (originally 380,460,270,270)
    int width = 580;
    int height = 640;
    int logoWidth = 480;
    int logoHeight = 540;

    // set reference point, paddings
    int paddingTop = logoHeight - 20;  // BiblePay Core
    int titleVersionVSpace = - 20;  // Version

    float fontFactor            = 1.0;
    float scale = qApp->devicePixelRatio();

    // define text to place
    QString titleText       = tr(PACKAGE_NAME);
    QString versionText = QString::fromStdString(FormatFullVersion()).remove(0, 1);
    QString titleAddText    = networkStyle->getTitleAddText();
	titleAddText = "Empowering the Tribulation Saints";
    QFont fontNormal = GUIUtil::getFontNormal();
    QFont fontBold = GUIUtil::getFontBold();

	QString splashScreenPath = ":/images/bitcoin";
 
	QPixmap pixmap7 = QPixmap(splashScreenPath);

	QPixmap pixmapLogo = networkStyle->getSplashImage();
    pixmapLogo.setDevicePixelRatio(scale);

    // Adjust logo color based on the current theme
	if (false)
	{
		QImage imgLogo = pixmapLogo.toImage().convertToFormat(QImage::Format_ARGB32);
		QColor logoColor = GUIUtil::getThemedQColor(GUIUtil::ThemedColor::BLUE);
		for (int x = 0; x < imgLogo.width(); ++x) {
			for (int y = 0; y < imgLogo.height(); ++y) {
				const QRgb rgb = imgLogo.pixel(x, y);
				imgLogo.setPixel(x, y, qRgba(logoColor.red(), logoColor.green(), logoColor.blue(), qAlpha(rgb)));
			}
		}
		pixmapLogo.convertFromImage(imgLogo);
	}

	pixmap = QPixmap(width * scale, height * scale);
    pixmap.setDevicePixelRatio(scale);
	pixmap.fill(GUIUtil::getThemedQColor(GUIUtil::ThemedColor::BORDER_WIDGET));
	
	
    QPainter pixPaint(&pixmap);

    QRect rect = QRect(1, 1, width - 2, height - 2);
    pixPaint.fillRect(rect, GUIUtil::getThemedQColor(GUIUtil::ThemedColor::BACKGROUND_WIDGET));

    pixPaint.drawPixmap((width / 2) - (logoWidth / 2), (height / 2) - (logoHeight / 2) + 20, pixmapLogo.scaled(logoWidth * scale, logoHeight * scale, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	pixPaint.drawPixmap((width / 2) - (logoWidth / 2), (height / 2) - (logoHeight / 2) + 20, pixmap7);

    pixPaint.setPen(GUIUtil::getThemedQColor(GUIUtil::ThemedColor::DEFAULT));
	pixPaint.setPen(QColor(100,100,100));
	pixPaint.setPen(QColor(255,0,0)); // Red (Version)
	pixPaint.setPen(QColor(192,192,192)); // Silver

    // check font size and drawing with
    fontBold.setPointSize(50 * fontFactor);
    pixPaint.setFont(fontBold);
    QFontMetrics fm = pixPaint.fontMetrics();
    int titleTextWidth = fm.width(titleText);
    if (titleTextWidth > width * 0.8) {
        fontFactor = 0.75;
    }

    fontBold.setPointSize(50 * fontFactor);
    pixPaint.setFont(fontBold);
    fm = pixPaint.fontMetrics();
    titleTextWidth  = fm.width(titleText);
    int titleTextHeight = fm.height();
	// BiblePay Core:
    pixPaint.drawText((width / 2) - (titleTextWidth / 2), titleTextHeight + paddingTop, titleText);

	// Version:
    fontNormal.setPointSize(16 * fontFactor);
    pixPaint.setFont(fontNormal);
    fm = pixPaint.fontMetrics();
    int versionTextWidth = fm.width(versionText);
	int nLeftPadding = 55;
	// Version+Release:
    pixPaint.drawText(nLeftPadding, titleTextHeight + paddingTop + titleVersionVSpace - 60, versionText);

    // draw additional text if special network
    if(!titleAddText.isEmpty()) {
        fontBold.setPointSize(10 * fontFactor);
        pixPaint.setFont(fontBold);
        fm = pixPaint.fontMetrics();
        int titleAddTextWidth = fm.width(titleAddText);
        // Draw the badge backround with the network-specific color
        QRect badgeRect = QRect(width - titleAddTextWidth - 20, 5, width, fm.height() + 10);

        QColor badgeColor = networkStyle->getBadgeColor();
        pixPaint.fillRect(badgeRect, badgeColor);
		// Network name:
		pixPaint.setPen(QColor(255, 255, 255));
		QString networkName = fProd ? "MainNet" : "TestNet";
		pixPaint.drawText(width - (titleAddTextWidth/2) - nLeftPadding, 20, networkName);
   	
		// Draw the text itself using white color, regardless of the current theme
        pixPaint.setPen(QColor(255, 255, 255));
		// Our Slogan:
        pixPaint.drawText(width - titleAddTextWidth - nLeftPadding, paddingTop + 20, titleAddText);
    }

    pixPaint.end();

    // Resize window and move to center of desktop, disallow resizing

    QRect r(QPoint(), QSize(width, height));
    resize(r.size());
    setFixedSize(r.size());
    move(QApplication::desktop()->screenGeometry().center() - r.center());
	
    subscribeToCoreSignals();
    installEventFilter(this);
}

SplashScreen::~SplashScreen()
{
    unsubscribeFromCoreSignals();
}

bool SplashScreen::eventFilter(QObject * obj, QEvent * ev) {
    if (ev->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
        if(keyEvent->text()[0] == 'q') {
            StartShutdown();
        }
    }
    return QObject::eventFilter(obj, ev);
}

void SplashScreen::slotFinish(QWidget *mainWin)
{
    Q_UNUSED(mainWin);

    /* If the window is minimized, hide() will be ignored. */
    /* Make sure we de-minimize the splashscreen window before hiding */
    if (isMinimized())
        showNormal();
    hide();
    deleteLater(); // No more need for this
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom | Qt::AlignHCenter),
        Q_ARG(QColor, QColor(255,215,0))); 
	    // BBP - Gold / Bezaleel
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress, bool resume_possible)
{
    InitMessage(splash, title + std::string("\n") +
            (resume_possible ? _("(press q to shutdown and continue later)")
                                : _("press q to shutdown")) +
            strprintf("\n%d", nProgress) + "%");
}

#ifdef ENABLE_WALLET
void SplashScreen::ConnectWallet(CWallet* wallet)
{
    wallet->ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2, false));
    connectedWallets.push_back(wallet);
}
#endif

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.InitMessage.connect(boost::bind(InitMessage, this, _1));
    uiInterface.ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2, _3));
#ifdef ENABLE_WALLET
    uiInterface.LoadWallet.connect(boost::bind(&SplashScreen::ConnectWallet, this, _1));
#endif
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.InitMessage.disconnect(boost::bind(InitMessage, this, _1));
    uiInterface.ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2, _3));
#ifdef ENABLE_WALLET
    uiInterface.LoadWallet.disconnect(boost::bind(&SplashScreen::ConnectWallet, this, _1));
    for (CWallet* const & pwallet : connectedWallets) {
        pwallet->ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2, false));
    }
#endif
}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    QFont messageFont = GUIUtil::getFontNormal();
    messageFont.setPointSize(14);
    painter.setFont(messageFont);
    painter.drawPixmap(0, 0, pixmap);
    QRect r = rect().adjusted(5, 5, -5, -15);
    painter.setPen(curColor);
    painter.drawText(r, curAlignment, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event)
{
    StartShutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
