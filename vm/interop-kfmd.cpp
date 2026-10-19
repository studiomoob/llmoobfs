// SPDX-License-Identifier: GPL-2.0-only
// Interoperability probe helper (vm/interop-guest.sh): reads and writes a file's user metadata
// through KDE's KFileMetaData::UserMetaData (KF5), the class Dolphin and Baloo use for tags,
// rating and comment. Control characters are printed as \xNN so a type byte shows.
//   kfmd show <file>
//   kfmd settags <file> <a,b,...>
//   kfmd setrating <file> <n>
//   kfmd setcomment <file> <text>
#include <KFileMetaData/UserMetaData>
#include <QCoreApplication>
#include <QStringList>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using KFileMetaData::UserMetaData;

static QByteArray esc(const QString &s)
{
	QString o;
	for (const QChar c : s) {
		if (c.unicode() < 0x20 || c.unicode() == 0x7f)
			o += QString::asprintf("\\x%02x", c.unicode());
		else if (c.unicode() == 0xfffd)
			o += QStringLiteral("\\ufffd");
		else
			o += c;
	}
	return o.toUtf8();
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);

	if (argc < 3) {
		fprintf(stderr, "usage: kfmd show|settags|setrating|setcomment <file> [value]\n");
		return 64;
	}
	UserMetaData md(QString::fromLocal8Bit(argv[2]));
	if (!strcmp(argv[1], "show") && argc == 3) {
		printf("isSupported=%d\n", md.isSupported());
		printf("tags=[%s] (count %d)\n", esc(md.tags().join(QStringLiteral("|"))).constData(),
		       (int)md.tags().size());
		printf("rating=%d\n", md.rating());
		printf("userComment=[%s]\n", esc(md.userComment()).constData());
		printf("attribute(moob.kind)=[%s]\n", esc(md.attribute(QStringLiteral("moob.kind"))).constData());
		printf("attribute(moob.score)=[%s]\n", esc(md.attribute(QStringLiteral("moob.score"))).constData());
		return 0;
	}
	if (argc != 4)
		return 64;
	UserMetaData::Error e;
	if (!strcmp(argv[1], "settags"))
		e = md.setTags(QString::fromLocal8Bit(argv[3]).split(QLatin1Char(','), Qt::SkipEmptyParts));
	else if (!strcmp(argv[1], "setrating"))
		e = md.setRating(atoi(argv[3]));
	else if (!strcmp(argv[1], "setcomment"))
		e = md.setUserComment(QString::fromLocal8Bit(argv[3]));
	else
		return 64;
	printf("%s: Error=%d\n", argv[1], (int)e);
	return e == UserMetaData::NoError ? 0 : 1;
}
