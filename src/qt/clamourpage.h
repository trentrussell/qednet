#ifndef CLAMOURPAGE_H
#define CLAMOURPAGE_H

#include <QWidget>

namespace Ui {
class ClamourPage;
}
class WalletModel;
class CClamour;

class ClamourPage : public QWidget
{
    Q_OBJECT

public:
    explicit ClamourPage(QWidget *parent = 0);
    ~ClamourPage();

    void setModel(WalletModel *model);

public slots:
    void showClamourTxResult(std::string txID, std::string txError);
    void setClamourSearchResults(CClamour *pResult);

private slots:
    void on_createPetitionEdit_textChanged();

    void on_createPetitionButton_clicked();

    void on_setVotesButton_clicked();

    void on_searchClamourButton_clicked();

private:
    Ui::ClamourPage *ui;
    WalletModel *model;

    void loadVotes();
    void saveVotes();
    void clearSearchTable();
};

#endif // CLAMOURPAGE_H
