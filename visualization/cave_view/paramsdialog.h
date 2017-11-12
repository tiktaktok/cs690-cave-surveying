#ifndef PARAMSDIALOG_H
#define PARAMSDIALOG_H

#include <QDialog>

#include "params.h"

namespace Ui {
class ParamsDialog;
}

class ParamsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ParamsDialog(QWidget *parent = 0);
    ~ParamsDialog();

    Params getParams();

public Q_SLOTS:
    void activateMeshAlgorithm(int index);
    void configureMesh();

protected:
    MeshAlgorithm meshAlgorithm;
    MeshParams meshParams;
    int activeMeshAlgorithmIndex;

    MeshAlgorithm getSelectedMeshAlgorithm();

private:
    Ui::ParamsDialog *ui;
};

#endif // PARAMSDIALOG_H