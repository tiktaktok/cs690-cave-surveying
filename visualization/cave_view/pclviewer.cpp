#include "pclviewer.h"
#include "build/ui_pclviewer.h"
#include "params.h"
#include "paramsdialog.h"
#include "paramsloader.h"

#include <pcl/io/pcd_io.h>
#include <pcl/io/vtk_io.h>
#include <pcl/search/kdtree.h>
#include <pcl/surface/mls.h>
#include <pcl/surface/poisson.h>
#include <pcl/features/normal_3d_omp.h>
#include <pcl/surface/gp3.h>
#include <pcl/surface/marching_cubes_hoppe.h>

#include <QApplication>
#include <QColorDialog>
#include <QDialog>
#include <QFileDialog>
#include <QIcon>
#include <QProgressDialog>

PCLViewer::PCLViewer(QWidget *parent) :
        QMainWindow(parent),
        ui(new Ui::PCLViewer)
{
    ui->setupUi(this);
    this->setWindowTitle("Cave Viewer");
    this->setWindowIcon(QIcon(":/duck.ico"));

    // Setup the cloud pointer
    cloud.reset(new PointCloudT);

    // Set up the QVTK window
    viewer.reset(new pcl::visualization::PCLVisualizer("viewer", false));
    viewer->setShowFPS(false);
    setBackgroundColor(0.2f);
    ui->qvtkWidget->SetRenderWindow(viewer->getRenderWindow());
    viewer->setupInteractor(ui->qvtkWidget->GetInteractor(), ui->qvtkWidget->GetRenderWindow());
    ui->qvtkWidget->update();

    // Connect buttons
    connect(ui->loadFileButton, SIGNAL(clicked()), this, SLOT(loadFileButtonPressed()));
    connect(ui->changeParametersButton, SIGNAL(clicked()), this, SLOT(changeParameters()));
    connect(ui->resetButton, SIGNAL(clicked()), this, SLOT(resetCamera()));

    // Connect checkboxes
    connect(ui->showPointsCheckbox, SIGNAL(toggled(bool)), this, SLOT(showPointsCheckBoxToggled(bool)));
    connect(ui->showNormalsCheckbox, SIGNAL(toggled(bool)), this, SLOT(showNormalsCheckboxToggled(bool)));
    connect(ui->showMeshCheckbox, SIGNAL(toggled(bool)), this, SLOT(showMeshCheckBoxToggled(bool)));

    // Connect radio buttons
    connect(ui->meshAsSurfaceRadioButton, SIGNAL(toggled(bool)), this, SLOT(radioButtonToggled(bool)));
    connect(ui->meshAsPointsRadioButton, SIGNAL(toggled(bool)), this, SLOT(radioButtonToggled(bool)));
    connect(ui->meshAsPointsRadioButton, SIGNAL(toggled(bool)), this, SLOT(radioButtonToggled(bool)));

    // Connect sliders
    connect(ui->backgroundSlider, SIGNAL(sliderMoved(int)), this, SLOT(setBackgroundColorInt(int)));
    connect(ui->backgroundSlider, SIGNAL(valueChanged(int)), this, SLOT(setBackgroundColorInt(int)));

    // Controls inactive until file is loaded
    disableUi();
    ui->filenameLabel->setText(QString::Null());

    this->raise();
    this->activateWindow();
    grabKeyboard();
}

void
PCLViewer::setBackgroundColor(float grayLevel)
{
    viewer->setBackgroundColor(grayLevel, grayLevel, grayLevel);
    ui->qvtkWidget->update();
}

void
PCLViewer::setBackgroundColorInt(int percents)
{
    setBackgroundColor(percents / 100.0f);
}

void
PCLViewer::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_W) {
        ui->meshAsWireframeRadioButton->setChecked(true);
        showMeshAsWireframe();
    } else if (event->key() == Qt::Key_P) {
        ui->meshAsPointsRadioButton->setChecked(true);
        showMeshAsPoints();
    } else if (event->key() == Qt::Key_S) {
        ui->meshAsSurfaceRadioButton->setChecked(true);
        showMeshAsSurface();
    } else if (event->key() == Qt::Key_J) {
        viewer->saveScreenshot("screenshot.png");
    }

    QMainWindow::keyReleaseEvent(event);
}

void
PCLViewer::setBackgroundBlack()
{
    ui->backgroundSlider->setValue(0);
    setBackgroundColor(0.0f);
}

void
PCLViewer::showMeshAsWireframe()
{
    viewer->setRepresentationToWireframeForAllActors();
    if (ui->backgroundSlider->value() > 60) {
        setBackgroundBlack();
    }
    ui->qvtkWidget->update();
}

void
PCLViewer::showMeshAsPoints()
{
    viewer->setRepresentationToPointsForAllActors();
    if (ui->backgroundSlider->value() > 60) {
        setBackgroundBlack();
    }
    ui->qvtkWidget->update();
}

void
PCLViewer::showMeshAsSurface()
{
    viewer->setRepresentationToSurfaceForAllActors();
    ui->qvtkWidget->update();
}

void
PCLViewer::changeParameters()
{
    loadPcdFile(lastFilename);
}

void
PCLViewer::loadFileButtonPressed()
{
    QString fileName = QFileDialog::getOpenFileName(
            this,
            "Load point cloud data file",
            QDir::currentPath(),
            "PCD files (*.pcd)"
    );

    if (fileName.isNull()) return;

    loadPcdFile(fileName.toStdString());
}

void
PCLViewer::loadPcdFile(std::string filename)
{
    std::cout << "Loading file: " << filename << std::endl;

    disableUi();

    QProgressDialog progress("Reading points...", "Cancel", 0, 5, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    //progress.setRange(0, 0); // undetermined progress bar
    progress.show();

    progress.setValue(0);
    if (progress.wasCanceled()) { return; }

    // Copied / inspired from:
    // http://www.pointclouds.org/assets/icra2012/surface.pdf

    pcl::io::loadPCDFile(filename.c_str(), *cloud);

    ParamsLoader paramsLoader = filename;
    Params previousParams;
    Params *p_previousParams = nullptr;

    if (paramsLoader.exists()) {
        previousParams = paramsLoader.read();
        p_previousParams = &previousParams;
    }

    ParamsDialog dialog(this, p_previousParams);
    if (dialog.exec() == QDialog::Rejected) { // cancelled?
        enableUi();
        return;
    }
    ui->changeParametersButton->setEnabled(true);
    Params params = dialog.getParams();
    paramsLoader.write(params);

    bool removeOutliersChanged = removeOutliersParamsChanged(p_previousParams, &params) || filename != lastFilename;
    bool smoothingChanged = smoothingParamsChanged(p_previousParams, &params) || filename != lastFilename;
    bool normalsChanged = normalsParamsChanged(p_previousParams, &params) || filename != lastFilename;
    lastFilename = filename;

    if (removeOutliersChanged) {
        if (params.removeOutliers) {
            removeOutliers();
        }
        colorize();
    }

    if (removeOutliersChanged || smoothingChanged) {
        if (!updateProgress(1, "Smoothing", &progress)) return;
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr *pCloud_smoothed;

        if (params.mlsEnabled) {
            pcl::MovingLeastSquares<pcl::PointXYZRGB, pcl::PointXYZRGB> mls;
            mls.setInputCloud(cloud);
            mls.setSearchRadius(params.mlsSearchRadius);
            mls.setPolynomialFit(true);
            mls.setPolynomialOrder(params.mlsPolynomialOrder);
            mls.setUpsamplingMethod(pcl::MovingLeastSquares<pcl::PointXYZRGB, pcl::PointXYZRGB>::SAMPLE_LOCAL_PLANE);
            mls.setUpsamplingRadius(params.mlsUpsamplingRadius);
            mls.setUpsamplingStepSize(params.mlsUpsamplingStepSize);

            pCloud_smoothed = new pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>());
            this->cloud_smoothed = pCloud_smoothed;
            mls.process(**pCloud_smoothed);
        } else {
            pCloud_smoothed = new pcl::PointCloud<pcl::PointXYZRGB>::Ptr(cloud);
        }

        this->cloud_smoothed = pCloud_smoothed;
    }

    if ((removeOutliersChanged || smoothingChanged || normalsChanged) && params.meshAlgorithm != noMesh) {
        if (!updateProgress(2, "Estimating normals", &progress)) return;

        pcl::NormalEstimationOMP<pcl::PointXYZRGB, pcl::Normal> ne;
        ne.setNumberOfThreads(params.normalsThreads);
        ne.setInputCloud(*cloud_smoothed);
        ne.setRadiusSearch(params.normalsSearchRadius);
        Eigen::Vector4f centroid;
        //pcl::compute3DCentroid(**cloud_smoothed, centroid);
        //ne.setViewPoint(centroid[0], centroid[1], centroid[2]);
        ne.setViewPoint(0.0f, 0.0f, 0.0f);

        cloud_normals = new pcl::PointCloud<pcl::Normal>::Ptr(new pcl::PointCloud<pcl::Normal>());
        ne.compute(**cloud_normals);

        /*
        for (size_t i = 0; i < cloud_normals->size(); ++i) {
            cloud_normals->points[i].normal_x *= -1;
            cloud_normals->points[i].normal_y *= -1;
            cloud_normals->points[i].normal_z *= -1;
        }
        */

        if (!updateProgress(3, "Concatenating points and normals", &progress)) return;

        cloud_smoothed_normals = new pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr(
                new pcl::PointCloud<pcl::PointXYZRGBNormal>());
        pcl::concatenateFields(**cloud_smoothed, **cloud_normals, **cloud_smoothed_normals);
    }

    if (params.meshAlgorithm != noMesh) {
        if (!updateProgress(4, "Mesh reconstruction", &progress)) return;

        pcl::PolygonMesh *pMesh = new pcl::PolygonMesh;
        this->mesh = pMesh;

        switch (params.meshAlgorithm) {
            case poisson:
                applyPoisson(params.meshParams.poissonParams);
                break;

            case greedyProjectionTriangulation:
                applyGreedyProjectionTriangulation(params.meshParams.greedyProjectionTriangulationParams);
                break;

            case marchingCubes:
                applyMarchingCubes(params.meshParams.marchingCubesParams);
                break;

            default:
                std::cerr << "No mesh algorithm has been picked." << std::endl;
        }

        ui->showMeshCheckbox->setEnabled(true);
    } else {
        mesh = nullptr;
    }

    // Set progress to 100 % and close progress dialog
    progress.setValue(5);

    viewer->removeAllPointClouds();
    viewer->removeAllShapes();

    if (ui->showPointsCheckbox->isChecked()) {
        addPointCloud();
    }

    if (ui->showNormalsCheckbox->isChecked()) {
        showNormals();
    }

    if (ui->showMeshCheckbox->isChecked() && mesh != nullptr) {
        viewer->addPolygonMesh(*mesh, "mesh");
    }

    viewer->resetCamera();
    ui->qvtkWidget->update();

    enableUi();
    if (params.meshAlgorithm == noMesh) {
        ui->showMeshCheckbox->setEnabled(false);
        ui->showNormalsCheckbox->setEnabled(false);
        ui->showMeshCheckbox->setChecked(false);
        ui->meshAsSurfaceRadioButton->setEnabled(false);
        ui->meshAsPointsRadioButton->setEnabled(false);
        ui->meshAsWireframeRadioButton->setEnabled(false);
    }

    QFileInfo fi(QString::fromStdString(filename));
    ui->filenameLabel->setText(fi.fileName());

    this->raise();
    this->activateWindow();
}

void
PCLViewer::addPointCloud()
{
    viewer->addPointCloud(*this->cloud_smoothed, "cloud_smoothed");
    viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "cloud_smoothed");
}

void
PCLViewer::showNormalsCheckboxToggled(bool checked)
{
    if (checked) showNormals();
    else hideNormals();
}

void
PCLViewer::showNormals()
{
    if (!viewer->contains("cloud_normals")) {
        viewer->addPointCloudNormals<pcl::PointXYZRGB, pcl::Normal>(
                *cloud_smoothed, *cloud_normals, 10, 10.0f, "cloud_normals");
        ui->qvtkWidget->update();
    }
}

void
PCLViewer::hideNormals()
{
    if (viewer->contains("cloud_normals")) {
        viewer->removePointCloud("cloud_normals");
        ui->qvtkWidget->update();
    }
}

void
PCLViewer::showPointsCheckBoxToggled(bool checked)
{
    if (checked && !viewer->contains("cloud_smoothed")) {
        addPointCloud();
    } else if (viewer->contains("cloud_smoothed")) {
        viewer->removePointCloud("cloud_smoothed");
    }
    ui->qvtkWidget->update();
}

void
PCLViewer::showMeshCheckBoxToggled(bool checked)
{
    if (checked && !viewer->contains("mesh")) {
        viewer->addPolygonMesh(*this->mesh, "mesh");

        ui->meshAsPointsRadioButton->setEnabled(true);
        ui->meshAsWireframeRadioButton->setEnabled(true);
        ui->meshAsSurfaceRadioButton->setEnabled(true);

        radioButtonToggled(true);
    } else if (viewer->contains("mesh")) {
        viewer->removePolygonMesh("mesh");

        ui->meshAsPointsRadioButton->setEnabled(false);
        ui->meshAsWireframeRadioButton->setEnabled(false);
        ui->meshAsSurfaceRadioButton->setEnabled(false);
    }
    ui->qvtkWidget->update();
}

void
PCLViewer::radioButtonToggled(bool state)
{
    if (ui->meshAsPointsRadioButton->isChecked()) {
        showMeshAsPoints();
    } else if (ui->meshAsWireframeRadioButton->isChecked()) {
        showMeshAsWireframe();
    } else if (ui->meshAsSurfaceRadioButton->isChecked()) {
        showMeshAsSurface();
    }
}

void
PCLViewer::disableUi()
{
    releaseKeyboard();
    setUiEnabled(false);
}

void
PCLViewer::enableUi()
{
    grabKeyboard();
    setUiEnabled(true);
}

void
PCLViewer::setUiEnabled(bool enabled)
{
    ui->showPointsCheckbox->setEnabled(enabled);
    ui->showNormalsCheckbox->setEnabled(enabled);
    ui->showMeshCheckbox->setEnabled(enabled);

    ui->meshAsPointsRadioButton->setEnabled(enabled);
    ui->meshAsSurfaceRadioButton->setEnabled(enabled);
    ui->meshAsWireframeRadioButton->setEnabled(enabled);

    ui->resetButton->setEnabled(enabled);

    ui->backgroundSlider->setEnabled(enabled);
}

bool
PCLViewer::updateProgress(int step, QString message, QProgressDialog *dialog)
{
    std::cout << "Progress (" << step << "): " << message.toUtf8().constData() << std::endl;
    if (dialog->wasCanceled()) return false;
    dialog->setValue(step);
    dialog->setLabelText(message);
    QApplication::processEvents();
    return true;
}

void
PCLViewer::resetCamera()
{
    viewer->resetCamera();
    ui->qvtkWidget->update();
}

float
PCLViewer::distance(pcl::PointCloud<pcl::PointXYZRGB>::iterator it)
{
    return sqrt(it->x * it->x + it->y * it->y + it->z * it->z);
}

float
PCLViewer::distance(pcl::PointXYZRGB p)
{
    return sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
}

void
PCLViewer::colorize()
{
    double maxDistance = 0.0;

    for (auto it = cloud->begin(); it < cloud->end(); ++it) {
        float d = distance(it);
        if (d > maxDistance) maxDistance = d;
    }

    std::cout << "Max distance: " << maxDistance << std::endl;

    for (auto it = cloud->begin(); it < cloud->end(); ++it) {
        float d = distance(it);
        auto value = static_cast<uint8_t>(255 * d / maxDistance);
        it->r = value;
        it->g = static_cast<uint8_t>(255 - value);
        it->b = 64;
    }
}

void
PCLViewer::removeOutliers()
{
    std::sort(cloud->begin(), cloud->end(), [this](pcl::PointXYZRGB &p1, pcl::PointXYZRGB &p2) {
        float d1 = distance(p1);
        float d2 = distance(p2);
        return d1 < d2;
    });

    int len = cloud->size();
    std::cout << "Original size: " << cloud->size() << std::endl;
    int lowIdx = static_cast<int>(len * 0.02);
    int highIdx = static_cast<int>(len * 0.99);
    int countUp = len - highIdx;

    auto it = cloud->begin();

    for (int i = 0; i < lowIdx; ++i) {
        it++;
    }
    float lowVal = distance(it);

    it = cloud->end();
    --it;
    for (int i = countUp; i >= 0; i--) {
        it--;
    }
    float highVal = distance(it);

    it = cloud->begin();
    for (it = cloud->begin(); it < cloud->end();) {
        auto it2 = it;
        ++it;
        if (distance(it) < lowVal || distance(it) > highVal) {
            cloud->erase(it2);
        }
    }

    it = cloud->end();
    --it;
    for (int i = countUp; i >= 0; --i) {
        auto it2 = it;
        --it;
        cloud->erase(it2);
    }

    std::cout << "New size: " << cloud->size() << std::endl;
}

void
PCLViewer::applyPoisson(PoissonParams poissonParams)
{
    std::cout << "Computing mesh using Poisson" << std::endl;

    pcl::Poisson<pcl::PointXYZRGBNormal> poisson;
    poisson.setDepth(poissonParams.poissonDepth);
    poisson.setInputCloud(*cloud_smoothed_normals);
    poisson.reconstruct(*mesh);
}

void
PCLViewer::applyGreedyProjectionTriangulation(GreedyProjectionTriangulationParams params)
{
    std::cout << "Computing mesh using GreedyProjectionTriangulation" << std::endl;

    pcl::GreedyProjectionTriangulation<pcl::PointXYZRGBNormal> gp3;
    gp3.setMaximumNearestNeighbors(params.maxNearestNeighbors);
    gp3.setSearchRadius(params.searchRadius);
    gp3.setMu(params.mu);
    gp3.setInputCloud(*cloud_smoothed_normals);
    gp3.reconstruct(*mesh);
}

void
PCLViewer::applyMarchingCubes(MarchingCubesParams params)
{
    std::cout << "Computing mesh using Marching Cubes" << std::endl;

    pcl::MarchingCubesHoppe<pcl::PointXYZRGBNormal> mc;

    mc.setIsoLevel(params.isoLevel);
    mc.setGridResolution(params.gridResolutionX, params.gridResolutionY, params.gridResolutionZ);
    mc.setPercentageExtendGrid(params.gridExtensionPercentage);

    /*
    pcl::search::KdTree<pcl::PointNormal>::Ptr tree (new pcl::search::KdTree<pcl::PointNormal>);
    tree->setInputCloud (*cloud_smoothed_normals);
    mc.setSearchMethod(tree);
    */
    mc.setInputCloud(*cloud_smoothed_normals);
    mc.reconstruct(*mesh);
}

PCLViewer::~PCLViewer()
{
    delete ui;
}
