//
//    Copyright (c) 2016 Jeff Thompson <jefft@threeputt.org>
//
//    This file is part of Inkjet Plumber.
//
//    Inkjet Plumber is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Inkjet Plumber is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with Inkjet Plumber.  If not, see <http://www.gnu.org/licenses/>.
//

#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(SparkleAutoUpdater* updater, QWidget* parent)
    : QMainWindow(parent)
    , about_dlg_(nullptr)
    , app_menu_(nullptr)
    , model_(nullptr)
    , preferences_dlg_(nullptr)
    , printer_(QPrinter::HighResolution)
    , timer_(nullptr)
    , ui(new Ui::MainWindow)
    , updater_(updater)
{
    ui->setupUi(this);

    setUnifiedTitleAndToolBarOnMac(true);
    setWindowIcon(QIcon(":/icons/InkjetPlumber.icns"));

    qRegisterMetaType<MaintenanceJob>("MaintenanceJob");
    qRegisterMetaType<MaintenanceJob*>("MaintenanceJob*");

    ui->listWidget->setFont(QFont("Courier New", 10));

    QStringList printer_list = QPrinterInfo::availablePrinterNames();

    for (int i = 0; i < printer_list.count(); i++)
    {
        QString printer_name = printer_list[i];
        read_printer_settings(printer_name);
        show_printer_info(printer_list[i]);
    }

    timer_ = new QTimer();
    connect(timer_, &QTimer::timeout, this, &MainWindow::timer_expired);
    timer_->start(60000);

    tray_.setIcon(QIcon(":/icons/InkjetPlumber.icns"));
    tray_.show();

    about_dlg_ = new AboutDialog(this);
    preferences_dlg_ = new PreferencesDialog(this);

    connect(preferences_dlg_, &PreferencesDialog::update_maint_job, this, &MainWindow::maint_job_updated);
    connect(this, &MainWindow::update_maint_job, this, &MainWindow::maint_job_updated);

    // Create menu items and actions
    menuBar()->setNativeMenuBar(true);
    app_menu_ = new QMenu("Inkjet Plumber App Menu", this);
    QAction* action_about = app_menu_->addAction("About Inkjet Plumber...");
    action_about->setShortcut(Qt::CTRL+Qt::Key_A);
    action_about->setObjectName("action_about");
    action_about->setMenuRole(QAction::AboutRole);
    QAction* action_prefs = app_menu_->addAction("Preferences...");
    action_prefs->setShortcut(Qt::CTRL+Qt::Key_Comma);
    action_prefs->setObjectName("action_prefs");
    action_prefs->setMenuRole(QAction::PreferencesRole);
    QAction* action_update = app_menu_->addAction("Check for Update...");
    action_update->setObjectName("action_update");
    action_update->setMenuRole(QAction::ApplicationSpecificRole);
    QAction* action_exit = app_menu_->addAction("Exit");
    action_exit->setObjectName("action_exit");
    action_exit->setMenuRole(QAction::QuitRole);
    menuBar()->addMenu(app_menu_);

    connect(action_about, &QAction::triggered, this, &MainWindow::show_about_dialog);
    connect(action_prefs, &QAction::triggered, this, &MainWindow::show_preferences_dialog);
    connect(action_update, &QAction::triggered, this, &MainWindow::check_for_update);
    connect(action_exit, &QAction::triggered, this, &MainWindow::close);

    // setup sparkle for auto updates
    setup_sparkle();

    return;
}

MainWindow::~MainWindow()
{
    if (about_dlg_)
    {
        delete about_dlg_;
        about_dlg_ = nullptr;
    }

    if (app_menu_)
    {
        delete app_menu_;
        app_menu_ = nullptr;
    }

    if (model_)
    {
        delete model_;
        model_ = nullptr;
    }

    if (preferences_dlg_)
    {
        delete preferences_dlg_;
        preferences_dlg_ = nullptr;
    }

    if (timer_)
    {
        timer_->stop();
        delete timer_;
        timer_ = nullptr;
    }

    if (ui)
    {
        delete ui;
        ui = nullptr;
    }

    if (updater_)
    {
        delete updater_;
        updater_ = nullptr;
    }

    qDeleteAll(maint_job_map_);
    maint_job_map_.clear();

    return;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    Q_UNUSED(event);
    return;
}

MaintenanceJob* MainWindow::find_maint_job(const QString& printer_name) const
{
    MaintenanceJob* job = nullptr;

    if (maint_job_map_.contains(printer_name))
        job = maint_job_map_.value(printer_name);

    return job;
}

QString MainWindow::get_duplex_string(QPrinter::DuplexMode mode) const
{
    QString mode_str;

    switch (mode)
    {
        case QPrinter::DuplexNone:
        {
            mode_str = "None";
            break;
        }
        case QPrinter::DuplexAuto:
        {
            mode_str = "Auto";
            break;
        }
        case QPrinter::DuplexLongSide:
        {
            mode_str = "Long Side";
            break;
        }
        case QPrinter::DuplexShortSide:
        {
            mode_str = "Short Side";
            break;
        }
        default:
        {
            mode_str = "Unknonwn";
            break;
        }
    }

    return mode_str;
}

QString MainWindow::get_pagesize_string(const QPageSize& page_size) const
{
    QString page_size_str;

    QSizeF size = page_size.size(QPageSize::Inch);

    qreal width = size.width();
    qreal height = size.height();

    QString width_str = QString::number(width, 'g');
    QString height_str = QString::number(height, 'g');

    page_size_str = width_str + " x " + height_str + " inches";

    return page_size_str;
}

QString MainWindow::get_state_string(QPrinter::PrinterState state) const
{
    QString state_str;

    switch (state)
    {
    case QPrinter::Idle:
        state_str = "Idle";
        break;
    case QPrinter::Active:
        state_str = "Active";
        break;
    case QPrinter::Aborted:
        state_str = "Aborted";
        break;
    case QPrinter::Error:
        state_str = "Error";
        break;
    default:
        state_str = "Unknown";
        break;
    }

    return state_str;
}

QString MainWindow::get_unit_string(QPageSize::Unit unit) const
{
    QString unit_str;

    switch (unit)
    {
    case QPageSize::Millimeter:
        unit_str = "millimeters";
        break;
    case QPageSize::Point:
        unit_str = "points";
        break;
    case QPageSize::Inch:
        unit_str = "inches";
        break;
    case QPageSize::Pica:
        unit_str = "picas";
        break;
    case QPageSize::Didot:
        unit_str = "didot";
        break;
    case QPageSize::Cicero:
        unit_str = "cicero";
        break;
    default:
        unit_str = "Unknown";
        break;
    }

    return unit_str;
}

void MainWindow::log_message(const QString& msg) const
{
    QDateTime now = QDateTime::currentDateTime();
    ui->listWidget->addItem(now.toString("yyyy-MM-dd hh:mm:ss ") + msg);
    return;
}

void MainWindow::paint_page(MaintenanceJob* job, QPrinter* printer)
{
    QPainter painter;
    painter.begin(printer);

    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::black);
//  Draw page rect to show printable page (excluding margins)
//  painter.drawRect(painter_rect);
    painter.setFont(QFont("Tahoma", 10));
    painter.drawText(0, 0, "Inkjet Plumber maintenance job sent to " + printer->printerName() + ": " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss."));
    painter.setPen(QPen(Qt::black));

    int offset = 0;

    if (job->cyan)          print_swatch(painter, &offset, 50, Qt::cyan);
    if (job->magenta)       print_swatch(painter, &offset, 50, Qt::magenta);
    if (job->yellow)        print_swatch(painter, &offset, 50, Qt::yellow);
    if (job->black)         print_swatch(painter, &offset, 50, Qt::black);
    if (job->gray)          print_swatch(painter, &offset, 50, Qt::gray);
    if (job->light_gray)    print_swatch(painter, &offset, 50, Qt::lightGray);
    if (job->red)           print_swatch(painter, &offset, 50, Qt::red);
    if (job->green)         print_swatch(painter, &offset, 50, Qt::green);
    if (job->blue)          print_swatch(painter, &offset, 50, Qt::blue);

    painter.end();

    log_message("Maintenance job sent to printer " + printer->printerName() + ".");

    return;
}

void MainWindow::print_swatch(QPainter& painter, int *x, int y, QColor color) const
{
    const int width = 150;
    const int height = 150;
    const int padding = 50;

    QString color_name = color.name();

    QFont courier("Courier New", 8);
    painter.setFont(courier);
    painter.setPen(QPen(Qt::black));

    QFontMetrics fm(painter.fontMetrics());

    int text_width = fm.width(color_name);
    int center_text = *x + (width - text_width) / 2;
    QRectF rect(center_text, y, 150, 150);

    painter.drawText(rect, color_name);

    painter.fillRect(*x, y + padding, width, height, QBrush(color));

    int new_y = y + height + padding*2;

    painter.setPen(QPen(color));
    painter.drawRect(*x, new_y, width, height);

    for (int i = 0; i < 150; i+=10)
    {
        painter.drawLine(*x, new_y+i, *x+width, new_y+i+10);
    }

    *x += 200;

    return;
}

void MainWindow::read_printer_settings(const QString& printer_name)
{
    MaintenanceJob* job = find_maint_job(printer_name);

    if (!job)
    {
        job = new MaintenanceJob();
        job->printer_name = printer_name;
        maint_job_map_.insert(printer_name, job);
    }

    QSettings s;
    s.beginGroup(printer_name);
    job->enabled = s.value("enabled", false).toBool();
    job->hours = s.value("hours", 55).toInt();
    job->cyan = s.value("cyan", true).toBool();
    job->yellow = s.value("yellow", true).toBool();
    job->magenta = s.value("magenta", true).toBool();
    job->black = s.value("black", true).toBool();
    job->gray = s.value("gray", true).toBool();
    job->light_gray = s.value("light_gray", true).toBool();
    job->red = s.value("red", true).toBool();
    job->green = s.value("green", true).toBool();
    job->blue = s.value("blue", true).toBool();
    job->last_maint = s.value("last_maint", QDateTime()).toDateTime();
    s.endGroup();

    return;
}

void MainWindow::run_maint_job(MaintenanceJob* job)
{
    if (!job) return;
    if (!job->enabled) return;

    bool colors = job->cyan|job->yellow|job->magenta|job->black|job->gray|job->light_gray|job->red|job->green|job->blue;

    if (!colors) return;

    job->last_maint = QDateTime::currentDateTime();

    QPrinter printer(QPrinter::HighResolution);
    QPageLayout page_layout = QPageLayout(QPageSize(QPageSize::Letter), QPageLayout::Portrait, QMarginsF(.5, .5, .5, .5), QPageLayout::Inch);

    printer.setPrinterName(job->printer_name);
    printer.setPageLayout(page_layout);
    printer.setCopyCount(1);
    printer.setDoubleSidedPrinting(false);
    printer.setDuplex(QPrinter::DuplexNone);
    printer.setColorMode(QPrinter::Color);
    printer.setPaperSize(QPrinter::Letter);
    printer.setPaperSource(QPrinter::Auto);
    printer.setCreator("Inkjet Plumber");
    printer.setPageLayout(page_layout);
    printer.setDocName("Inkjet Plumber Maintenance Job");

    QPrintDialog print_dlg(&printer);
    print_dlg.accept();

    paint_page(job, &printer);

    QSettings s;
    s.beginGroup(job->printer_name);
    s.setValue("last_maint", job->last_maint);
    s.endGroup();
    s.sync();

    return;
}

void MainWindow::setup_sparkle()
{
#if defined(INKJETPLUMBER_DEBUG)
    long interval = (60*60*1);
#else
    long interval = (60*60*24);
#endif
    if (updater_)
    {
        updater_->setUpdateCheckInterval(interval);
        QDateTime dt = updater_->lastUpdateCheckDate();
        qDebug("Last update check performed at '%s'.", qUtf8Printable(dt.toString("yyyy-MM-dd hh:mm:ss")));
    }

    return;
}

void MainWindow::check_for_update()
{
    if (updater_)
        updater_->checkForUpdates();

    return;
}

void MainWindow::show_printer_info(const QString& printer_name) const
{
    QString msg;
    QPrinterInfo pi = QPrinterInfo::printerInfo(printer_name);

    MaintenanceJob *job = find_maint_job(printer_name);
    if (!job) return;

    QDateTime next_maint = job->last_maint.addSecs(job->hours*3600);

    if (job->enabled)
        log_message("Found printer: " + printer_name + ", maint job is enabled.");
    else
        log_message("Found printer: " + printer_name + ", maint job is disabled.");

    return;
}

void MainWindow::write_printer_settings(const QString& printer_name)
{
    MaintenanceJob* job = find_maint_job(printer_name);
    write_printer_settings(job);
    return;
}

void MainWindow::write_printer_settings(MaintenanceJob *job)
{
    if (job)
    {
        QSettings s;
        s.beginGroup(job->printer_name);
        s.setValue("enabled", job->enabled);
        s.setValue("hours", job->hours);
        s.setValue("cyan", job->cyan);
        s.setValue("yellow", job->yellow);
        s.setValue("magenta", job->magenta);
        s.setValue("black", job->black);
        s.setValue("gray", job->gray);
        s.setValue("light_gray", job->light_gray);
        s.setValue("red", job->red);
        s.setValue("green", job->green);
        s.setValue("blue", job->blue);
        s.setValue("last_maint", job->last_maint);
        s.endGroup();
    }

    return;
}

void MainWindow::maint_job_updated(MaintenanceJob *job)
{
    if (!job) return;

    if (!maint_job_map_.contains(job->printer_name))
    {
        maint_job_map_.insert(job->printer_name, job);
        show_printer_info(job->printer_name);
    }

    write_printer_settings(job->printer_name);

    if (preferences_dlg_)
        preferences_dlg_->set_maintenance_map(maint_job_map_);

    return;
}

void MainWindow::show_about_dialog()
{
    if (about_dlg_)
    {
        about_dlg_->show();
    }

    return;
}

void MainWindow::show_preferences_dialog()
{

    if (preferences_dlg_)
    {
        preferences_dlg_->set_maintenance_map(maint_job_map_);
        int result = preferences_dlg_->exec();
        Q_UNUSED(result);
    }

    return;
}

void MainWindow::timer_expired()
{
    QDateTime now = QDateTime::currentDateTime();

    // check to see if any new printers have been installed since startup

    QStringList printers = QPrinterInfo::availablePrinterNames();

    for (int i = 0; i < printers.count(); i++)
    {
        QString printer_name = printers[i];
        if (!maint_job_map_.contains(printer_name))
        {
            MaintenanceJob* job = new MaintenanceJob(printer_name);
            emit update_maint_job(job);
        }
    }

    // Process each maint job

    MaintenanceJobMapIterator it(maint_job_map_);

    while (it.hasNext())
    {
        it.next();
        MaintenanceJob *job = it.value();
        if (!job->enabled) continue;
        QDateTime next_maint = job->last_maint.addSecs(job->hours*3600);
        if (!job->last_maint.isValid() || now > next_maint)
        {
            run_maint_job(job);
        }
    }

    return;
}