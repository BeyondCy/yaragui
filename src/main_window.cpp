#include "main_window.h"
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
#include <boost/assign.hpp>
#include <iostream>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMenu>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QClipboard>
#include <QtCore/QMimeData>

#ifdef WIN32
  #undef min
  #undef max
#endif

MainWindow::~MainWindow()
{
  if (!isMaximized()) {
    QByteArray state = saveGeometry();
    QString encodedState(state.toBase64());
    m_settings->setMainWindowGeometry(encodedState.toStdString());
  }
}

MainWindow::MainWindow(boost::asio::io_service& io, boost::shared_ptr<Settings> settings) : m_io(io), m_settings(settings)
{
  m_ui.setupUi(this);
  setAcceptDrops(true); /* enable drag and drop */
  setWindowIcon(QIcon(":/yaragui.png"));

  /* load window state */
  QByteArray windowState = QByteArray::fromStdString(m_settings->getMainWindowGeometry());
  restoreGeometry(QByteArray::fromBase64(windowState));

  QMenu* menu = new QMenu(this);
  m_ui.targetButton->setMenu(menu);

  QAction* scanDirectory = menu->addAction("Scan &Directory");
  scanDirectory->setIcon(QIcon(":/glyphicons-441-folder-closed.png"));
  connect(scanDirectory, SIGNAL(triggered()), this, SLOT(handleTargetDirectoryBrowse()));

  menu->addSeparator();
  QAction* about = menu->addAction("&About");
  about->setIcon(QIcon(":/glyphicons-196-info-sign.png"));
  connect(about, SIGNAL(triggered()), this, SLOT(handleAboutMenu()));

  m_ui.targetButton->setIcon(QIcon(":/glyphicons-145-folder-open.png"));
  m_ui.ruleButton->setIcon(QIcon(":/glyphicons-145-folder-open.png"));

  connect(m_ui.targetButton, SIGNAL(released()), this, SLOT(handleTargetFileBrowse()));
  connect(m_ui.ruleButton, SIGNAL(released()), this, SLOT(handleRuleFileBrowse()));

  m_ui.tree->setColumnCount(2);
  m_ui.tree->header()->hide();
  m_ui.tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_ui.tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_ui.tree->header()->setStretchLastSection(false);
  connect(m_ui.tree, SIGNAL(itemSelectionChanged()), this, SLOT(treeItemSelectionChanged()));

  /* copy meny for tree view */
  m_copyMenuAction = new QAction("&Copy", this);
  connect(m_copyMenuAction, SIGNAL(triggered()), this, SLOT(handleCopyItemClicked()));
  m_copyMenuAction->setIcon(QIcon::fromTheme("edit-copy"));
  m_copyMenuAction->setEnabled(false);
  m_ui.tree->addAction(m_copyMenuAction);
  m_ui.tree->setContextMenuPolicy(Qt::ActionsContextMenu);

  m_targetPanel = new TargetPanel(this);
  m_ui.splitter->addWidget(m_targetPanel);
  m_matchPanel = new MatchPanel(this);
  m_ui.splitter->addWidget(m_matchPanel);

  /* set up the status bar */
  m_stopButton = new QToolButton(this);
  connect(m_stopButton, SIGNAL(clicked()), this, SLOT(handleScanAbortButton()));
  m_stopButton->setIcon(QIcon(":/glyphicons-176-stop.png"));
  m_stopButton->setIconSize(QSize(16, 16));
  m_stopButton->setFixedWidth(m_stopButton->height());
  m_stopButton->hide();
  m_ui.statusBar->addPermanentWidget(m_stopButton);
  m_status = new QLabel(this);
  m_status->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  m_ui.statusBar->addPermanentWidget(m_status, 1);

  /* timer to control status bar animation */
  m_scanTimer = new QTimer(this);
  connect(m_scanTimer, SIGNAL(timeout()), this, SLOT(handleScanTimer()));

  m_status->setText("Drag file into window and select rule to scan");
  show();
}

void MainWindow::scanBegin()
{
  m_treeItems.clear();
  m_targetMap.clear();
  m_scannerRuleMap.clear();
  m_rulesetViewMap.clear();
  m_fileStats.clear();
  m_matchPanel->hide();
  m_targetPanel->hide();
  m_ui.tree->clear();

  m_ui.targetPath->setEnabled(false);
  m_ui.targetButton->setEnabled(false);
  m_ui.rulePath->setEnabled(false);
  m_ui.ruleButton->setEnabled(false);

  m_scanAborted = false;
  m_stopButton->show();
  m_stopButton->setEnabled(true);
  m_scanTimer->start(1000/10);
}

void MainWindow::scanEnd()
{
  m_ui.targetPath->setEnabled(true);
  m_ui.targetButton->setEnabled(true);
  m_ui.rulePath->setEnabled(true);
  m_ui.ruleButton->setEnabled(true);

  m_scanTimer->stop();
  m_stopButton->hide();

  if (!m_scanAborted) {
    m_status->setText("Operation complete");
  } else {
    m_status->setText("Scan aborted");
  }
}

void MainWindow::setCompilerBusy(bool state) {
  /* prevent scan starting during rule compilation */
  m_ui.targetPath->setEnabled(!state);
  m_ui.targetButton->setEnabled(!state);
  m_ui.rulePath->setEnabled(!state);
  m_ui.ruleButton->setEnabled(!state);
}

void MainWindow::setRules(const std::vector<RulesetView::Ref>& rules)
{
  m_rules = rules;

  QMenu* menu = new QMenu(this);
  m_ui.ruleButton->setMenu(menu);

  QAction* allRules = menu->addAction("&All Rules");
  allRules->setIcon(QIcon(":/glyphicons-320-sort.png"));
  connect(allRules, SIGNAL(triggered()), this, SLOT(handleSelectRuleAllFromMenu()));

  menu->addSeparator();

  m_signalMapper = new QSignalMapper(this);
  connect(m_signalMapper, SIGNAL(mapped(int)), this, SLOT(handleSelectRuleFromMenu(int)));

  for (size_t i = 0; i < rules.size(); ++i) {
    QAction* action = 0;
    if (rules[i]->hasName()) {
      action = menu->addAction(rules[i]->name().c_str());
    } else {
      action = menu->addAction(rules[i]->fileNameOnly().c_str());
    }
    if (!rules[i]->isCompiled()) {
      action->setText(action->text() + "*");
    }
    action->setIcon(QIcon(":/glyphicons-319-more-items.png"));
    connect(action, SIGNAL(triggered()), m_signalMapper, SLOT(map()));
    m_signalMapper->setMapping(action, int(i));
  }

  if (!rules.empty()) {
    menu->addSeparator();
  }

  QAction* configure = menu->addAction("&Configure");
  configure->setIcon(QIcon(":/glyphicons-137-cogwheel.png"));
  connect(configure, SIGNAL(triggered()), this, SLOT(handleEditRulesMenu()));
}

void MainWindow::addScanResult(const std::string& target, ScannerRule::Ref rule, RulesetView::Ref view)
{
  QTreeWidgetItem* root = 0;
  if (m_treeItems.find(target) != m_treeItems.end()) {
    root = m_treeItems[target];
  } else {
    root = new QTreeWidgetItem(m_ui.tree);
    m_treeItems[target] = root;
    m_targetMap[root] = target; /* reverse map */
    m_ui.tree->insertTopLevelItem(0, root);
    root->setText(0, target.c_str());
    root->setText(1, tr("No matches"));

    /* set the file icon */
    QFileInfo fileInfo(target.c_str());
    root->setIcon(0, m_iconProvider.icon(fileInfo));
  }

  if (!rule) {
    return;
  }

  QTreeWidgetItem* item = new QTreeWidgetItem(root);
  m_scannerRuleMap[item] = rule;
  m_rulesetViewMap[item] = view;

  item->setText(0, rule->identifier.c_str());

  if (view->hasName()) {
    item->setText(1, view->name().c_str());
  } else {
    item->setText(1, view->fileNameOnly().c_str());
  }

  if (root->childCount() == 1) {
    root->setText(1, tr("1 match"));
  } else {
    root->setText(1, tr("%1 matches").arg(root->childCount()));
  }

  if (root->childCount() == 1) {
    root->setExpanded(true); /* only expand when we add the first item */
  }
}

void MainWindow::updateFileStats(FileStats::Ref stats)
{
  m_fileStats[stats->filename()] = stats;
  if (m_targetPanel->isVisible() && m_targetPanel->filename() == stats->filename()) {
    m_targetPanel->show(stats->filename(), stats);
  }
}

void MainWindow::handleSelectRuleAllFromMenu()
{
  /* null pointer means scan with every rule */
  onChangeRuleset(RulesetView::Ref());
  m_ui.rulePath->setText(tr("(All Rules)"));
}

void MainWindow::handleSelectRuleFromMenu(int rule)
{
  RulesetView::Ref view = m_rules[rule];
  m_ui.rulePath->setText(view->file().c_str());
  onChangeRuleset(view);
}

void MainWindow::handleTargetFileBrowse()
{
  QString file = QFileDialog::getOpenFileName(this, "Select Target File", QString(), "All Files (*)");
  if (!file.isEmpty()) {
    file = QDir::toNativeSeparators(file);
    m_ui.targetPath->setText(file);
    m_ui.rulePath->setText(tr("")); /* need to select rules again */
    std::vector<std::string> targets;
    targets.push_back(file.toStdString());
    onChangeTargets(targets);
  }
}

void MainWindow::handleTargetDirectoryBrowse()
{
  QString dir = QFileDialog::getExistingDirectory(this, "Select Target Directory");
  if (!dir.isEmpty()) {
    dir = QDir::toNativeSeparators(dir);
    m_ui.targetPath->setText(dir);
    m_ui.rulePath->setText(tr("")); /* need to select rules again */
    std::vector<std::string> targets;
    targets.push_back(dir.toStdString());
    onChangeTargets(targets);
  }
}

void MainWindow::handleRuleFileBrowse()
{
  QString file = QFileDialog::getOpenFileName(this, "Select Rule File", QString(), "YARA Rules (*)");
  if (!file.isEmpty()) {
    file = QDir::toNativeSeparators(file);
    m_ui.rulePath->setText(file);
    onChangeRuleset(boost::make_shared<RulesetView>(file.toStdString()));
  }
}

void MainWindow::handleEditRulesMenu()
{
  onRequestRuleWindowOpen();
}

void MainWindow::handleAboutMenu()
{
  onRequestAboutWindowOpen();
}

void MainWindow::treeItemSelectionChanged()
{
  QList<QTreeWidgetItem *> items = m_ui.tree->selectedItems();

  m_copyMenuAction->setEnabled(items.size());

  if (!items.size()) {
    return;
  }

  QList<int> sizes = m_ui.splitter->sizes();
  int maxSize = std::max(sizes[1], sizes[2]);
  if (!m_targetPanel->isVisible() && !m_matchPanel->isVisible()) {
    /* first display, set default view size */
    QSize targetSize = m_targetPanel->maximumSize(), matchSize = m_matchPanel->maximumSize();
    maxSize = std::max(targetSize.height(), matchSize.height());
    maxSize = 200;
  }
  sizes[1] = maxSize;
  sizes[2] = maxSize;

  QTreeWidgetItem* selectedItem = items[0];
  if (m_targetMap.find(selectedItem) != m_targetMap.end()) {
    std::string target = m_targetMap[selectedItem];
    m_matchPanel->hide();
    m_targetPanel->show(target, m_fileStats[target]);
  } else {
    ScannerRule::Ref rule = m_scannerRuleMap[selectedItem];
    RulesetView::Ref view = m_rulesetViewMap[selectedItem];
    m_targetPanel->hide();
    m_matchPanel->show(rule, view);
  }

  m_ui.splitter->setSizes(sizes);
}

void MainWindow::handleScanTimer()
{
  std::vector<std::string> progress;
  boost::assign::push_back(progress)("|")("/")("-")("\\");
  m_scanPhase = (m_scanPhase + 1) % progress.size();
  std::string message = "[" + progress[m_scanPhase] + "] ";
  message += "Scanning...";
  m_status->setText(message.c_str());
}

void MainWindow::handleScanAbortButton()
{
  m_scanTimer->stop();
  m_stopButton->setEnabled(false);
  m_scanAborted = true;
  onScanAbort();
}

void MainWindow::handleCopyItemClicked()
{
  QList<QTreeWidgetItem*> items = m_ui.tree->selectedItems();
  if (items.size() != 1) {
    return;
  }
  QClipboard *clipboard = QApplication::clipboard();
  clipboard->clear();
  clipboard->setText(items[0]->text(0));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
  /* filter out non-files, like images dragged from the web browser */
  const QMimeData* mimeData = event->mimeData();
  if (!mimeData->hasUrls()) {
    event->ignore();
    return;
  }

  QList<QUrl> urls = mimeData->urls();
  for (int i = 0; i < urls.size(); ++i) {
    if (!urls[i].isLocalFile()) {
      event->ignore();
      return;
    }
  }

  event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
  const QMimeData* mimeData = event->mimeData();
  QList<QUrl> urls = mimeData->urls();

  /* if one file was dropped, check if it is a rule */
  if (urls.size() == 1) {
    QFileInfo fileInfo(urls[0].toLocalFile());
    QString file = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    if (fileInfo.suffix() == "yar" || fileInfo.suffix() == "yara") {
      m_ui.rulePath->setText(file);
      onChangeRuleset(boost::make_shared<RulesetView>(file.toStdString()));
      event->acceptProposedAction();
      return; /* a rule was dropped */
    }
  }

  /* treat all other files as targets */
  std::vector<std::string> targets;
  for (int i = 0; i < urls.size(); ++i) {
    QFileInfo fileInfo(urls[i].toLocalFile());
    QString file = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    targets.push_back(file.toStdString());
  }

  if (targets.size() == 1) {
    m_ui.targetPath->setText(targets[0].c_str());
  } else {
    m_ui.targetPath->setText(tr("(Multiple Targets)"));
  }
  m_ui.rulePath->setText(tr("")); /* need to select rules again */

  onChangeTargets(targets);
  event->acceptProposedAction();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
  switch(event->key())
  {
  case Qt::Key_Escape:
    close();
    break;
  default:
    QMainWindow::keyPressEvent(event);
  }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  /* dont let any open dialogs prevent application shutdown */
  QApplication::exit(0);
  event->accept();
}
