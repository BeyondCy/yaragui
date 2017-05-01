#ifndef __RULE_WINDOW_H__
#define __RULE_WINDOW_H__

#include "ui_rule_window.h"
#include "ruleset_view.h"
#include "settings.h"
#include <boost/signals2.hpp>
#include <boost/optional.hpp>

class RuleWindow : public QMainWindow
{
  Q_OBJECT

public:

  ~RuleWindow();
  RuleWindow(boost::shared_ptr<Settings> settings);

  boost::signals2::signal<void (const std::vector<RulesetView::Ref>& rules)> onSaveRules;
  boost::signals2::signal<void (RulesetView::Ref view)> onCompileRule;

  void setRules(const std::vector<RulesetView::Ref>& rules);

private slots:

  void handleButtonClicked(QAbstractButton* button);
  void handleItemEdit(QTableWidgetItem* item);
  void handleSelectionChanged();

  void handleCompileClicked();
  void handleMoveUpClicked();
  void handleMoveDownClicked();
  void handleRemoveClicked();

private:

  void dragEnterEvent(QDragEnterEvent* event);
  void dropEvent(QDropEvent* event);
  void closeEvent(QCloseEvent *closeEvent);

  void rulesToView(const std::vector<RulesetView::Ref>& rules);
  std::vector<RulesetView::Ref> selectedItems();
  boost::optional<int> selectedItemIndex();

  void keyPressEvent(QKeyEvent *event);

  boost::shared_ptr<Settings> m_settings;
  Ui::RuleWindow m_ui;

  std::vector<RulesetView::Ref> m_rules;
  std::map<QTableWidgetItem*, RulesetView::Ref> m_names;

  QAction* m_compileButton;
  QAction* m_moveUpButton;
  QAction* m_moveDownButton;
  QAction* m_removeButton;

  QPixmap m_iconYes;
  QPixmap m_iconNo;

};

#endif // __RULE_WINDOW_H__
