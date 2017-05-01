#include "main_controller.h"
#include <boost/make_shared.hpp>
#include <boost/foreach.hpp>

MainController::MainController(int argc, char* argv[], boost::asio::io_service& io) : m_io(io), m_haveRuleset(false), m_scanning(false), m_statsRemaining(0)
{
  m_settings = boost::make_shared<Settings>();

  m_rm = boost::make_shared<RulesetManager>(boost::ref(io), m_settings);
  m_rm->onScanResult.connect(boost::bind(&MainController::handleScanResult, this, _1, _2, _3));
  m_rm->onScanComplete.connect(boost::bind(&MainController::handleScanComplete, this, _1));
  m_rm->onRulesUpdated.connect(boost::bind(&MainController::handleRulesUpdated, this));

  m_sc = boost::make_shared<StatsCalculator>(boost::ref(io));
  m_sc->onFileStats.connect(boost::bind(&MainController::handleFileStats, this, _1));

  m_mainWindow = boost::make_shared<MainWindow>(boost::ref(io), m_settings);
  m_mainWindow->onChangeTargets.connect(boost::bind(&MainController::handleChangeTargets, this, _1));
  m_mainWindow->onChangeRuleset.connect(boost::bind(&MainController::handleChangeRuleset, this, _1));
  m_mainWindow->onRequestRuleWindowOpen.connect(boost::bind(&MainController::handleRequestRuleWindowOpen, this));
  m_mainWindow->onRequestAboutWindowOpen.connect(boost::bind(&MainController::handleAboutWindowOpen, this));
  m_mainWindow->onScanAbort.connect(boost::bind(&MainController::handleUserScanAbort, this));

  m_mainWindow->setRules(m_rm->getRules());
}

void MainController::handleChangeTargets(const std::vector<std::string>& files)
{
  m_targets = files;
}

void MainController::handleChangeRuleset(RulesetView::Ref ruleset)
{
  m_ruleset = ruleset;
  m_haveRuleset = true;
  scan();
}

void MainController::handleScanResult(const std::string& target, ScannerRule::Ref rule, RulesetView::Ref view)
{
  if (!rule) {
    /* scan of this target complete, compute stats for it */
    m_statsRemaining++;
    m_sc->getStats(target);
  }
  m_mainWindow->addScanResult(target, rule, view);
}

void MainController::handleScanComplete(const std::string& error)
{
  m_scanning = false;
  handleOperationsComplete();
}

void MainController::handleRulesUpdated()
{
  std::vector<RulesetView::Ref> rules = m_rm->getRules();
  m_mainWindow->setRules(rules);

  /* if any compile windows are open, reload compiler errors .etc */
  BOOST_FOREACH(RulesetView::Ref rule, rules) {
    updateCompileWindows(rule);
  }

  if (!m_scanning) {
    setCompileWindowsEnabled(true);
    m_mainWindow->setCompilerBusy(false);
  }
}

void MainController::handleFileStats(FileStats::Ref stats)
{
  m_statsRemaining--;
  m_mainWindow->updateFileStats(stats);
  handleOperationsComplete();
}

void MainController::handleRequestRuleWindowOpen()
{
  if (m_ruleWindow && m_ruleWindow->isVisible()) {
    m_ruleWindow->raise();
    return;
  }

  m_ruleWindow = boost::make_shared<RuleWindow>(m_settings);
  m_ruleWindow->onSaveRules.connect(boost::bind(&MainController::handleRuleWindowSave, this, _1));
  m_ruleWindow->onCompileRule.connect(boost::bind(&MainController::handleRuleWindowCompile, this, _1));
  m_ruleWindow->setRules(m_rm->getRules());
}

void MainController::handleRuleWindowSave(const std::vector<RulesetView::Ref>& rules)
{
  m_rm->updateRules(rules);
}

void MainController::handleRuleWindowCompile(RulesetView::Ref view)
{
  CompileWindow::Ref compileWindow = boost::make_shared<CompileWindow>(view);
  compileWindow->onRecompileRule.connect(boost::bind(&MainController::handleCompileWindowRecompile, this, _1));
  m_compileWindows.push_back(compileWindow);
  handleCompileWindowRecompile(view);
}

void MainController::handleCompileWindowRecompile(RulesetView::Ref view)
{
  m_mainWindow->setCompilerBusy(true);
  setCompileWindowsEnabled(false);
  m_rm->compile(view);
}

void MainController::handleAboutWindowOpen()
{
  if (m_aboutWindow && m_aboutWindow->isVisible()) {
    m_aboutWindow->raise();
  } else {
    m_aboutWindow = boost::make_shared<AboutWindow>(boost::ref(m_io), m_mainWindow->geometry());
  }
}

void MainController::handleUserScanAbort()
{
  m_rm->scanAbort();
  m_sc->abort();
}

void MainController::handleOperationsComplete()
{
  if (m_scanning || m_statsRemaining) {
    return; /* not done yet */
  }

  if (m_ruleWindow && m_ruleWindow->isVisible()) {
    m_ruleWindow->setRules(m_rm->getRules());
  }

  m_mainWindow->scanEnd();

  if (m_ruleWindow) {
    m_ruleWindow->setEnabled(true);
  }

  setCompileWindowsEnabled(true);
}

void MainController::scan()
{
  if (!m_targets.empty() && m_haveRuleset && !m_scanning) {
    m_scanning = true;
    m_mainWindow->scanBegin();
    m_sc->reset();
    m_rm->scan(m_targets, m_ruleset);
    if (m_ruleWindow) {
      m_ruleWindow->setEnabled(false);
    }
    setCompileWindowsEnabled(false);
  }
}

void MainController::updateCompileWindows(const RulesetView::Ref& rule)
{
  /* clean any dead windows */
  std::list<CompileWindow::Ref>::iterator i = m_compileWindows.begin();
  while (i != m_compileWindows.end()) {
    if (!(*i)->isVisible()) {
      i = m_compileWindows.erase(i);
    } else {
      i++;
    }
  }

  /* update any windows matching this rule */
  BOOST_FOREACH(CompileWindow::Ref window, m_compileWindows) {
    if (rule->file() == window->rule()->file()) {
      window->setRule(rule);
    }
  }
}

void MainController::setCompileWindowsEnabled(bool state)
{
  BOOST_FOREACH(CompileWindow::Ref window, m_compileWindows) {
    window->setEnabled(state);
  }
}
