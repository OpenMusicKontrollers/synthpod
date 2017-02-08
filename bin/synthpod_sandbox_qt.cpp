/*
 * Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include <atomic>

#include <sandbox_slave.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#if (SYNTHPOD_SANDBOX_QT == 4)
#	include <QtGui/QApplication>
#	include <QtGui/QMainWindow>
#elif (SYNTHPOD_SANDBOX_QT == 5)
#	include <QtWidgets/QApplication>
#	include <QtWidgets/QMainWindow>
#else
#	error "SYNTHPOD_QT is invalid"
#endif
#include <QtCore/QSocketNotifier>

typedef struct _app_t app_t;

struct _app_t {
	sandbox_slave_t *sb;

	QSocketNotifier *socknot;
	QMainWindow *win;
	QWidget *widget;
};

static QApplication *a;
static std::atomic<bool> done = ATOMIC_VAR_INIT(false);

class MySocketNotifier : public QSocketNotifier
{
	Q_OBJECT

public:
	MySocketNotifier(int socket, sandbox_slave_t *sb);
	~MySocketNotifier();

protected:
	bool event(QEvent *event);

protected:
	sandbox_slave_t *sb;
};

MySocketNotifier::MySocketNotifier(int socket, sandbox_slave_t *sb)
	: QSocketNotifier(socket, QSocketNotifier::Read), sb(sb)
{}

MySocketNotifier::~MySocketNotifier()
{}

bool
MySocketNotifier::event(QEvent *event)
{
	(void)event;
	sandbox_slave_recv(sb);
	sandbox_slave_flush(sb);
	return true;
}

class MyWindow : public QMainWindow
{
	Q_OBJECT

public:
	MyWindow();
	~MyWindow();

protected:
	void timerEvent(QTimerEvent *event);

protected:
	int timer_id;
};

MyWindow::MyWindow()
	: QMainWindow()
{
	timer_id = startTimer(100); // 0.1s
}

MyWindow::~MyWindow()
{
	killTimer(timer_id);
}

void
MyWindow::timerEvent(QTimerEvent *event)
{
	(void)event;
	if(done.load(std::memory_order_relaxed))
		a->quit();
}

static inline void
_sig(int signum)
{
	(void)signum;
	done.store(true, std::memory_order_relaxed);
}

static inline int
_init(sandbox_slave_t *sb, void *data)
{
	app_t *app= (app_t *)data;

	signal(SIGINT, _sig);

	int argc = 0;
	a = new QApplication(argc, NULL, true);
	app->win = new MyWindow();
	if(!app->win)
		return -1;

	const char *title = sandbox_slave_title_get(sb);
	if(title)
		app->win->setWindowTitle(title);

	const LV2_Feature parent_feature = {
		.URI = LV2_UI__parent,
		.data = (void *)app->win
	};

	if(!sandbox_slave_instantiate(sb, &parent_feature, (void *)&app->widget))
		return -1;
	if(!app->widget)
		return -1;

	int fd;
	sandbox_slave_fd_get(sb, &fd);
	if(fd == -1)
	{
		fprintf(stderr, "sandbox_slave_fd_get failed\n");
		return -1;
	}

	app->socknot = new MySocketNotifier(fd, sb);
	if(!app->socknot)
		return -1;

	app->widget->show();
	app->win->setCentralWidget(app->widget);

	app->win->adjustSize();
	app->win->show();

	return 0;
}

static inline void
_run(sandbox_slave_t *sb, float update_rate, void *data)
{
	app_t *app = (app_t *)data;
	(void)sb;

	a->exec();
}

static inline void
_deinit(void *data)
{
	app_t *app = (app_t *)data;

	delete app->socknot;
	app->win->hide();
	delete app->win;

	delete a;
}

static const sandbox_slave_driver_t driver = {
	.init_cb = _init,
	.run_cb = _run,
	.deinit_cb = _deinit,
	.resize_cb = NULL
};

int
main(int argc, char **argv)
{
	static app_t app;

	app.sb = sandbox_slave_new(argc, argv, &driver, &app);
	if(app.sb)
	{
		sandbox_slave_run(app.sb);
		sandbox_slave_free(app.sb);
		return 0;
	}

	return -1;
}

#if (SYNTHPOD_SANDBOX_QT == 4)
#	include <synthpod_sandbox_qt4_moc.h>
#elif (SYNTHPOD_SANDBOX_QT == 5)
#	include <synthpod_sandbox_qt5_moc.h>
#else
#	error "SYNTHPOD_QT is invalid"
#endif
